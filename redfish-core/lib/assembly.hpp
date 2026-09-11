// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "app.hpp"
#include "assembly_battery_cm.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/resource.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "led.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "task_messages.hpp"
#include "utils/assembly_utils.hpp"
#include "utils/asset_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/name_utils.hpp"
#include "utils/resource_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>

#include <chrono>
#include <cstddef>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{

static constexpr std::string_view cmBasePath = "/com/ibm/ConcurrentMaintenance";
static constexpr std::string_view cmAddPath =
    "/com/ibm/ConcurrentMaintenance/add";
static constexpr std::string_view cmRemovePath =
    "/com/ibm/ConcurrentMaintenance/remove";
static constexpr std::string_view cmProgressIface =
    "xyz.openbmc_project.Common.Progress";
// Maximum time to wait for the CM daemon to respond before task is set to
// exception state
static constexpr int cmTaskTimeoutMinutes = 30;

/**
 * @brief Map a Common.Progress Status value to a Redfish task terminal state.
 * @return true when terminal, false when still in progress.
 */
inline bool mapCmStatus(const std::string& status,
                        const std::shared_ptr<task::TaskData>& taskData)
{
    constexpr std::string_view completed =
        "xyz.openbmc_project.Common.Progress.OperationStatus.Completed";
    constexpr std::string_view failed =
        "xyz.openbmc_project.Common.Progress.OperationStatus.Failed";
    constexpr std::string_view aborted =
        "xyz.openbmc_project.Common.Progress.OperationStatus.Aborted";
    constexpr std::string_view inProgress =
        "xyz.openbmc_project.Common.Progress.OperationStatus.InProgress";

    if (status == completed)
    {
        taskData->messages.emplace_back(
            messages::taskCompletedOK(std::to_string(taskData->index)));
        taskData->state = "Completed";
        taskData->status = "OK";
        return true;
    }
    if (status == failed || status == aborted)
    {
        taskData->messages.emplace_back(
            messages::taskAborted(std::to_string(taskData->index)));
        taskData->state = "Exception";
        taskData->status = "Warning";
        return true;
    }
    if (status != inProgress)
    {
        BMCWEB_LOG_WARNING("CM task: unexpected Progress.Status value: {}",
                           status);
    }
    return false;
}

/**
 * @brief Check whether a CM operation is already in progress by looking for
 *        object under /com/ibm/ConcurrentMaintenance.
 * Calls onClear() when none exists.  Returns 503 resourceInUse if busy.
 *
 * @param asyncResp     Shared response, used only to set the error if busy.
 * @param inventoryPath Inventory path logged in the error message.
 * @param onClear       Callback invoked when CM is confirmed not busy.
 */
inline void checkCmNotBusy(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& inventoryPath,
                           std::function<void()> onClear)
{
    dbus::utility::getSubTreePaths(
        std::string(cmBasePath), 1, std::array<std::string_view, 0>{},
        [asyncResp, inventoryPath, onClear = std::move(onClear)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& paths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "getSubTreePaths failed for CM busy check on {}: {}",
                    inventoryPath, ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            if (!paths.empty())
            {
                BMCWEB_LOG_ERROR(
                    "CM is busy ({}): rejecting PATCH ReadyToRemove on {}",
                    paths.front(), inventoryPath);
                messages::resourceInUse(asyncResp->res);
                return;
            }
            onClear();
        });
}

/**
 * @brief Set ReadyToRemove on the inventory D-Bus object and start a task to
 *        track the CM operation.
 *
 * @param asyncResp     Shared response, populateResp() sets 202 Accepted.
 * @param payload       Task payload built from the original PATCH request.
 * @param inventoryPath D-Bus object path of the inventory FRU.
 * @param readyToRemove true: remove flow; false: add flow.
 */
inline void afterCheckCmNotBusy(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    task::Payload&& payload, const std::string& inventoryPath,
    bool readyToRemove)
{
    dbus::utility::getDbusObject(
        inventoryPath,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.State.ReadyToRemove"},
        [asyncResp, payload = std::move(payload), inventoryPath,
         readyToRemove](const boost::system::error_code& ec,
                        const dbus::utility::MapperGetObject& object) mutable {
            if (ec || object.empty())
            {
                BMCWEB_LOG_ERROR(
                    "getDbusObject failed for ReadyToRemove on {}: {}",
                    inventoryPath, ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string service = object.begin()->first;

            const std::string_view cmObjPath =
                readyToRemove ? cmRemovePath : cmAddPath;
            const std::string matchStr = std::format(
                "type='signal',"
                "interface='org.freedesktop.DBus.Properties',"
                "member='PropertiesChanged',"
                "path='{}'",
                cmObjPath);

            std::shared_ptr<task::TaskData> taskHandle =
                task::TaskData::createTask(
                    [](const boost::system::error_code& ec2,
                       sdbusplus::message_t& msg,
                       const std::shared_ptr<task::TaskData>& taskData)
                        -> bool {
                        // ec2 is only ever set on timer expiry it's set to
                        // operation_aborted
                        if (ec2)
                        {
                            BMCWEB_LOG_ERROR("CM task timed out for task {}",
                                             taskData->index);
                            return task::completed;
                        }

                        std::string iface;
                        dbus::utility::DBusPropertiesMap values;
                        msg.read(iface, values);

                        if (iface != cmProgressIface)
                        {
                            return !task::completed;
                        }
                        for (const auto& [propName, propVal] : values)
                        {
                            if (propName != "Status")
                            {
                                continue;
                            }
                            const std::string* status =
                                std::get_if<std::string>(&propVal);
                            if (status != nullptr &&
                                mapCmStatus(*status, taskData))
                            {
                                return task::completed;
                            }
                        }
                        return !task::completed;
                    },
                    matchStr);

            taskHandle->state = "Running";
            taskHandle->startTimer(std::chrono::minutes(cmTaskTimeoutMinutes));
            taskHandle->payload.emplace(std::move(payload));

            // Now set the property on inventory dbus. If the property write
            // fails, abort the task
            sdbusplus::asio::setProperty(
                *crow::connections::systemBus, service, inventoryPath,
                "xyz.openbmc_project.State.ReadyToRemove", "ReadyToRemove",
                readyToRemove,
                [asyncResp, taskHandle](const boost::system::error_code& ec3,
                                        const sdbusplus::message_t& /*msg*/) {
                    if (ec3)
                    {
                        BMCWEB_LOG_ERROR("Failed to set ReadyToRemove: {}",
                                         ec3.message());
                        taskHandle->messages.emplace_back(
                            messages::internalError());
                        taskHandle->state = "Exception";
                        taskHandle->status = "Warning";
                        taskHandle->finishTask();
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // Property write succeeded, commit 202 Accepted.
                    taskHandle->populateResp(asyncResp->res);
                });
        });
}

/**
 * @brief Create a Redfish task tracking a Concurrent Maintenance operation.
 *
 * @param asyncResp     Shared response populateResp() sets 202 Accepted.
 * @param payload       Task payload built from the original PATCH request.
 * @param inventoryPath D-Bus object path of the inventory FRU.
 * @param readyToRemove true: remove flow; false: add flow.
 */
inline void startCmTask(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        task::Payload&& payload,
                        const std::string& inventoryPath, bool readyToRemove)
{
    checkCmNotBusy(asyncResp, inventoryPath,
                   [asyncResp, payload = std::move(payload), inventoryPath,
                    readyToRemove]() mutable {
                       afterCheckCmNotBusy(asyncResp, std::move(payload),
                                           inventoryPath, readyToRemove);
                   });
}

/**
 * @brief Get Location code for the given assembly.
 * @param[in] asyncResp - Shared pointer for asynchronous calls.
 * @param[in] serviceName - Service in which the assembly is hosted.
 * @param[in] assembly - Assembly object.
 * @param[in] assemblyJsonPtr - json-keyname on the assembly list output.
 * @return None.
 */
inline void getAssemblyLocationCode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& serviceName, const std::string& assembly,
    const nlohmann::json::json_pointer& assemblyJsonPtr)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, serviceName, assembly,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp, assembly, assemblyJsonPtr](
            const boost::system::error_code& ec, const std::string& value) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR("DBUS response error: {} for assembly {}",
                                     ec.value(), assembly);
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            asyncResp->res.jsonValue[assemblyJsonPtr]["Location"]
                                    ["PartLocation"]["ServiceLabel"] = value;
        });
}

inline void getAssemblyReadyToRemove(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const auto& serviceName, const auto& assembly,
    const nlohmann::json::json_pointer& assemblyJsonPtr)
{
    std::string fru = sdbusplus::message::object_path(assembly).filename();
    if (fru == "panel0" || fru == "panel1")
    {
        dbus::utility::getProperty<bool>(
            serviceName, assembly, "xyz.openbmc_project.Inventory.Item",
            "Present",
            [asyncResp, assemblyJsonPtr,
             assembly](const boost::system::error_code& ec, const bool value) {
                if (ec)
                {
                    if (ec.value() != EBADR)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error: {}", ec.value());
                        messages::internalError(asyncResp->res);
                    }
                    return;
                }

                // Special handling for LCD and base panel CM.
                asyncResp->res.jsonValue[assemblyJsonPtr]["Oem"]["OpenBMC"]
                                        ["@odata.type"] =
                    "#OpenBMCAssembly.v1_0_0.OpenBMC";

                // if panel is not present, implies it is already removed or
                // can be placed.
                asyncResp->res.jsonValue[assemblyJsonPtr]["Oem"]["OpenBMC"]
                                        ["ReadyToRemove"] = !value;
            });
    }
}

/**
 * @brief Populate ReadyToRemove for assemblies that implement
 *        xyz.openbmc_project.State.ReadyToRemove.
 */
inline void afterGetAssemblyReadyToRemove(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const nlohmann::json::json_pointer& assemblyJsonPtr,
    const boost::system::error_code& ec, bool value)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error: {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }
    asyncResp->res.jsonValue[assemblyJsonPtr]["ReadyToRemove"] = value;
}

inline void getAssemblyReadyToRemove(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& serviceName, const std::string& assembly,
    const nlohmann::json::json_pointer& assemblyJsonPtr)
{
    dbus::utility::getProperty<bool>(
        serviceName, assembly, "xyz.openbmc_project.State.ReadyToRemove",
        "ReadyToRemove",
        std::bind_front(afterGetAssemblyReadyToRemove, asyncResp,
                        assemblyJsonPtr));
}

inline void afterSetAssemblyReadyToRemove(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& assembly, bool value,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec)
    {
        if (ec.value() == EBADR)
        {
            messages::propertyUnknown(asyncResp->res, "ReadyToRemove");
            return;
        }
        BMCWEB_LOG_ERROR("getDbusObject failed for ReadyToRemove on {}: {}",
                         assembly, ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    if (object.empty())
    {
        messages::propertyUnknown(asyncResp->res, "ReadyToRemove");
        return;
    }
    const std::string& service = object.begin()->first;
    setDbusProperty(asyncResp, "ReadyToRemove", service, assembly,
                    "xyz.openbmc_project.State.ReadyToRemove", "ReadyToRemove",
                    value);
}

inline void setAssemblyReadyToRemove(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& assembly, bool value)
{
    constexpr std::array<std::string_view, 1> readyToRemoveIface = {
        "xyz.openbmc_project.State.ReadyToRemove"};
    dbus::utility::getDbusObject(assembly, readyToRemoveIface,
                                 std::bind_front(afterSetAssemblyReadyToRemove,
                                                 asyncResp, assembly, value));
}

inline void afterGetDbusObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& assembly,
    const nlohmann::json::json_pointer& assemblyJsonPtr,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error : {} for assembly {}", ec.value(),
                         assembly);
        messages::internalError(asyncResp->res);
        return;
    }

    nlohmann::json::json_pointer ptr = assemblyJsonPtr;
    ptr /= "Name";
    name_util::getPrettyName(asyncResp, assembly, object, ptr);

    for (const auto& [serviceName, interfaceList] : object)
    {
        for (const auto& interface : interfaceList)
        {
            if (interface == "xyz.openbmc_project.Inventory.Decorator.Asset")
            {
                asset_utils::getAssetInfo(asyncResp, serviceName, assembly,
                                          assemblyJsonPtr, true, false);
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Decorator.LocationCode")
            {
                getAssemblyLocationCode(asyncResp, serviceName, assembly,
                                        assemblyJsonPtr);
            }
            else if (interface == "xyz.openbmc_project.Inventory.Item")
            {
                resource_utils::getResourceState(asyncResp, serviceName,
                                                 assembly, assemblyJsonPtr);

                getAssemblyReadyToRemove(asyncResp, serviceName, assembly,
                                         assemblyJsonPtr);
            }
            else if (interface ==
                     "xyz.openbmc_project.State.Decorator.OperationalStatus")
            {
                resource_utils::getResourceHealth(asyncResp, serviceName,
                                                  assembly, assemblyJsonPtr);
            }
            else if (interface == "xyz.openbmc_project.State.ReadyToRemove")
            {
                getAssemblyReadyToRemove(asyncResp, serviceName, assembly,
                                         assemblyJsonPtr);
            }
        }
    }
}

/**
 * @brief Get properties for the assemblies associated to given chassis
 * @param[in] asyncResp - Shared pointer for asynchronous calls.
 * @param[in] chassisId - Chassis the assemblies are associated with.
 * @param[in] assemblies - list of all the assemblies associated with the
 * chassis.
 * @return None.
 */
inline void getAssemblyProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::vector<std::string>& assemblies)
{
    BMCWEB_LOG_DEBUG("Get properties for assembly associated");

    std::size_t assemblyIndex = 0;
    for (const std::string& assembly : assemblies)
    {
        nlohmann::json::object_t item;
        item["@odata.type"] = "#Assembly.v1_6_0.AssemblyData";
        item["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/Assembly#/Assemblies/{}", chassisId,
            std::to_string(assemblyIndex));
        item["MemberId"] = std::to_string(assemblyIndex);
        item["Name"] = sdbusplus::message::object_path(assembly).filename();

        asyncResp->res.jsonValue["Assemblies"].emplace_back(item);

        nlohmann::json::json_pointer assemblyJsonPtr(
            "/Assemblies/" + std::to_string(assemblyIndex));

        // Handle special case for tod_battery assembly OEM ReadyToRemove
        // property NOTE: The following method for the special case of the
        // tod_battery ReadyToRemove property only works when there is only ONE
        // adcsensor handled by the adcsensor application.
        if (sdbusplus::message::object_path(assembly).filename() ==
            "tod_battery")
        {
            getReadyToRemoveOfTodBattery(asyncResp, assemblyIndex);
        }

        dbus::utility::getDbusObject(
            assembly, assemblyInterfaces,
            std::bind_front(afterGetDbusObject, asyncResp, assembly,
                            assemblyJsonPtr));

        getLocationIndicatorActive(
            asyncResp, assembly, [asyncResp, assemblyJsonPtr](bool asserted) {
                asyncResp->res
                    .jsonValue[assemblyJsonPtr]["LocationIndicatorActive"] =
                    asserted;
            });

        nlohmann::json& assemblyArray = asyncResp->res.jsonValue["Assemblies"];
        asyncResp->res.jsonValue["Assemblies@odata.count"] =
            assemblyArray.size();

        assemblyIndex++;
    }
}

inline void afterHandleChassisAssemblyGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const boost::system::error_code& ec,
    const std::vector<std::string>& assemblyList)
{
    if (ec)
    {
        BMCWEB_LOG_WARNING("Chassis {} not found", chassisID);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisID);
        return;
    }

    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/Assembly/Assembly.json>; rel=describedby");

    asyncResp->res.jsonValue["@odata.type"] = "#Assembly.v1_5_1.Assembly";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/Assembly", chassisID);
    asyncResp->res.jsonValue["Name"] = "Assembly Collection";
    asyncResp->res.jsonValue["Id"] = "Assembly";

    asyncResp->res.jsonValue["Assemblies"] = nlohmann::json::array();
    asyncResp->res.jsonValue["Assemblies@odata.count"] = 0;

    if (!assemblyList.empty())
    {
        getAssemblyProperties(asyncResp, chassisID, assemblyList);
    }
}

/**
 * @param[in] asyncResp - Shared pointer for asynchronous calls.
 * @param[in] chassisID - Chassis to which the assemblies are
 * associated.
 *
 * @return None.
 */
inline void handleChassisAssemblyGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG("Get chassis Assembly");
    assembly_utils::getChassisAssembly(
        asyncResp, chassisID,
        std::bind_front(afterHandleChassisAssemblyGet, asyncResp, chassisID));
}

inline void handleChassisAssemblyHead(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    assembly_utils::getChassisAssembly(
        asyncResp, chassisID,
        [asyncResp,
         chassisID](const boost::system::error_code& ec,
                    const std::vector<std::string>& /*assemblyList*/) {
            if (ec)
            {
                BMCWEB_LOG_WARNING("Chassis {} not found", chassisID);
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisID);
                return;
            }
            asyncResp->res.addHeader(
                boost::beast::http::field::link,
                "</redfish/v1/JsonSchemas/Assembly.json>; rel=describedby");
        });
}

inline void afterHandleChassisAssemblyPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID,
    std::vector<nlohmann::json::object_t>& assemblyData,
    task::Payload&& payload, const boost::system::error_code& ec,
    const std::vector<std::string>& assemblyList)
{
    if (ec)
    {
        BMCWEB_LOG_WARNING("Chassis {} not found", chassisID);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisID);
        return;
    }

    std::map<std::string, bool> locationIndicatorActiveMap;
    std::map<std::string, bool> readyToRemoveMap;
    std::map<std::string, nlohmann::json> oemIndicatorMap;

    for (nlohmann::json::object_t& item : assemblyData)
    {
        std::optional<std::string> memberId;
        std::optional<bool> locationIndicatorActive;
        std::optional<bool> readyToRemove;
        std::optional<nlohmann::json> oem;
        if (!json_util::readJsonObject(
                item, asyncResp->res, "MemberId", memberId,
                "LocationIndicatorActive", locationIndicatorActive,
                "ReadyToRemove", readyToRemove, "Oem", oem))
        {
            return;
        }
        if (locationIndicatorActive)
        {
            if (memberId)
            {
                locationIndicatorActiveMap[*memberId] =
                    *locationIndicatorActive;
            }
            else
            {
                BMCWEB_LOG_WARNING(
                    "Property Missing - MemberId must be included with "
                    "LocationIndicatorActive ");
                messages::propertyMissing(asyncResp->res, "MemberId");
                return;
            }
        }
        if (readyToRemove)
        {
            if (memberId)
            {
                readyToRemoveMap[*memberId] = *readyToRemove;
            }
            else
            {
                BMCWEB_LOG_WARNING(
                    "Property Missing - MemberId must be included with ReadyToRemove");
                messages::propertyMissing(asyncResp->res, "MemberId");
                return;
            }
        }
        if (oem)
        {
            if (memberId)
            {
                oemIndicatorMap[*memberId] = *oem;
            }
            else
            {
                BMCWEB_LOG_WARNING(
                    "Property Missing - MemberId must be included with Oem property");
                messages::propertyMissing(asyncResp->res, "MemberId");
                return;
            }
        }
    }

    // Only one ReadyToRemove CM operation is allowed per PATCH
    if (readyToRemoveMap.size() > 1)
    {
        messages::propertyNotWritable(asyncResp->res, "ReadyToRemove");
        return;
    }

    std::size_t assemblyIndex = 0;
    std::optional<std::string> cmAssemblyPath;
    std::optional<bool> cmReadyToRemove;

    for (const auto& assembly : assemblyList)
    {
        auto iter =
            locationIndicatorActiveMap.find(std::to_string(assemblyIndex));

        if (iter != locationIndicatorActiveMap.end())
        {
            setLocationIndicatorActive(asyncResp, assembly, iter->second);
        }

        auto iter2 = oemIndicatorMap.find(std::to_string(assemblyIndex));

        if (iter2 != oemIndicatorMap.end())
        {
            std::optional<bool> readytoremove;
            if (!json_util::readJson(iter2->second, asyncResp->res,
                                     "OpenBMC/ReadyToRemove", readytoremove))
            {
                BMCWEB_LOG_WARNING("Property Value Format Error ");
                messages::propertyValueFormatError(
                    asyncResp->res, iter2->second, "OpenBMC/ReadyToRemove");
                return;
            }

            if (!readytoremove)
            {
                BMCWEB_LOG_WARNING("Property Missing ");
                messages::propertyMissing(asyncResp->res,
                                          "OpenBMC/ReadyToRemove");
                return;
            }

            // Handle special case for tod_battery assembly OEM ReadyToRemove
            // property. NOTE: The following method for the special case of the
            // tod_battery ReadyToRemove property only works when there is only
            // ONE adcsensor handled by the adcsensor application.
            if (sdbusplus::message::object_path(assembly).filename() ==
                "tod_battery")
            {
                doBatteryCM(asyncResp, assembly, readytoremove.value());
            }

            // Special handling for LCD and base panel. This is required to
            // support concurrent maintenance for base and LCD panel.
            else if (sdbusplus::message::object_path(assembly).filename() ==
                         "panel0" ||
                     sdbusplus::message::object_path(assembly).filename() ==
                         "panel1")
            {
                // Based on the status of readytoremove flag, inventory data
                // like CCIN and present property needs to be updated for this
                // FRU.
                // readytoremove as true implies FRU has been prepared for
                // removal. Set action as "deleteFRUVPD". This is the api
                // exposed by vpd-manager to clear CCIN and set present
                // property as false for the FRU.
                // readytoremove as false implies FRU has been replaced. Set
                // action as "CollectFRUVPD". This is the api exposed by
                // vpd-manager to recollect vpd for a given FRU.
                std::string action = "CollectFRUVPD";
                if (readytoremove.value())
                {
                    action = "deleteFRUVPD";
                }

                dbus::utility::async_method_call(
                    [asyncResp, action](const boost::system::error_code& ec1) {
                        if (ec1)
                        {
                            BMCWEB_LOG_ERROR(
                                "Call to Manager failed for action: {} with error: {}",
                                action, ec1.value());
                            messages::internalError(asyncResp->res);
                            return;
                        }
                    },
                    "com.ibm.VPD.Manager", "/com/ibm/VPD/Manager",
                    "com.ibm.VPD.Manager", action,
                    sdbusplus::message::object_path(assembly));
            }
            else
            {
                BMCWEB_LOG_WARNING(
                    "Property Unknown: ReadyToRemove on Assembly with MemberID: {}",
                    assemblyIndex);
                messages::propertyUnknown(asyncResp->res, "ReadyToRemove");
                return;
            }
        }

        auto iter3 = readyToRemoveMap.find(std::to_string(assemblyIndex));
        if (iter3 != readyToRemoveMap.end())
        {
            cmAssemblyPath = assembly;
            cmReadyToRemove = iter3->second;
        }

        assemblyIndex++;
    }

    if (cmAssemblyPath)
    {
        startCmTask(asyncResp, std::move(payload), *cmAssemblyPath,
                    *cmReadyToRemove);
    }
}

inline void handleChassisAssemblyPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::vector<nlohmann::json::object_t> assemblyData;
    if (!redfish::json_util::readJsonPatch(req, asyncResp->res, "Assemblies",
                                           assemblyData))
    {
        return;
    }

    auto payload = task::Payload(req);
    assembly_utils::getChassisAssembly(
        asyncResp, chassisID,
        [asyncResp, chassisID, assemblyData = std::move(assemblyData),
         payload = std::move(payload)](
            const boost::system::error_code& ec,
            const std::vector<std::string>& assemblyList) mutable {
            afterHandleChassisAssemblyPatch(asyncResp, chassisID, assemblyData,
                                            std::move(payload), ec,
                                            assemblyList);
        });
}

/**
 * Systems derived class for delivering Assembly Schema.
 */
inline void requestRoutesAssembly(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Assembly/")
        .privileges(redfish::privileges::headAssembly)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleChassisAssemblyHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Assembly/")
        .privileges(redfish::privileges::getAssembly)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleChassisAssemblyGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Assembly/")
        .privileges(redfish::privileges::patchAssembly)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleChassisAssemblyPatch, std::ref(app)));
}

} // namespace redfish
