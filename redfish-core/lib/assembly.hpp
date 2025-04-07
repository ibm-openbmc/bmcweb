#pragma once

#include "app.hpp"
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
#include "utils/chassis_utils.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"

#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
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

/**
 * @brief Get Asset properties on the given assembly.
 * @param[in] asyncResp - Shared pointer for asynchronous calls.
 * @param[in] serviceName - Service in which the assembly is hosted.
 * @param[in] assembly - Assembly object.
 * @param[in] assemblyIndex - Index on the assembly object.
 * @return None.
 */
void getAssemblyAsset(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const auto& serviceName, const auto& assembly,
                      const auto& assemblyIndex)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, serviceName, assembly,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [asyncResp, assemblyIndex](
            const boost::system::error_code& ec1,
            const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec1)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec1.value());
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string* partNumber = nullptr;
            const std::string* serialNumber = nullptr;
            const std::string* sparePartNumber = nullptr;
            const std::string* model = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), propertiesList, "PartNumber",
                partNumber, "SerialNumber", serialNumber, "SparePartNumber",
                sparePartNumber, "Model", model);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            nlohmann::json& assemblyArray =
                asyncResp->res.jsonValue["Assemblies"];
            nlohmann::json& assemblyData = assemblyArray.at(assemblyIndex);

            if (partNumber != nullptr)
            {
                assemblyData["PartNumber"] = *partNumber;
            }

            if (serialNumber != nullptr)
            {
                assemblyData["SerialNumber"] = *serialNumber;
            }

            if (sparePartNumber != nullptr)
            {
                assemblyData["SparePartNumber"] = *sparePartNumber;
            }

            if (model != nullptr)
            {
                assemblyData["Model"] = *model;
            }
        });
}

/**
 * @brief Get Location code for the given assembly.
 * @param[in] asyncResp - Shared pointer for asynchronous calls.
 * @param[in] serviceName - Service in which the assembly is hosted.
 * @param[in] assembly - Assembly object.
 * @param[in] assemblyIndex - Index on the assembly object.
 * @return None.
 */
void getAssemblyLocationCode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const auto& serviceName, const auto& assembly, const auto& assemblyIndex)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, serviceName, assembly,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp, assemblyIndex](const boost::system::error_code& ec1,
                                   const std::string& value) {
            if (ec1)
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec1.value());
                messages::internalError(asyncResp->res);
                return;
            }

            nlohmann::json& assemblyArray =
                asyncResp->res.jsonValue["Assemblies"];
            nlohmann::json& assemblyData = assemblyArray.at(assemblyIndex);

            assemblyData["Location"]["PartLocation"]["ServiceLabel"] = value;
        });
}

inline void afterGetReadyToRemoveOfTodBattery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::size_t assemblyIndex, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& /*unused*/)
{
    nlohmann::json& assemblyArray = asyncResp->res.jsonValue["Assemblies"];
    if (ec)
    {
        if (ec.value() == boost::system::errc::io_error)
        {
            // Battery voltage is not on DBUS so ADCSensor is not
            // running.
            nlohmann::json& oemOpenBMC =
                assemblyArray.at(assemblyIndex)["Oem"]["OpenBMC"];
            oemOpenBMC["@odata.type"] = "#OpenBMCAssembly.v1_0_0.OpenBMC";
            oemOpenBMC["ReadyToRemove"] = true;
            return;
        }
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }
    nlohmann::json& oemOpenBMC =
        assemblyArray.at(assemblyIndex)["Oem"]["OpenBMC"];
    oemOpenBMC["@odata.type"] = "#OpenBMCAssembly.v1_0_0.OpenBMC";
    oemOpenBMC["ReadyToRemove"] = false;
}

inline void getReadyToRemoveOfTodBattery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::size_t assemblyIndex)
{
    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/sensors/voltage/Battery_Voltage", {},
        std::bind_front(afterGetReadyToRemoveOfTodBattery, asyncResp,
                        assemblyIndex));
}

void getAssemblyPresence(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const auto& serviceName, const auto& assembly,
                         const auto& assemblyIndex)
{
    nlohmann::json& assemblyArray = asyncResp->res.jsonValue["Assemblies"];
    nlohmann::json& assemblyData = assemblyArray.at(assemblyIndex);

    assemblyData["Status"]["State"] = resource::State::Enabled;

    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, serviceName, assembly,
        "xyz.openbmc_project.Inventory.Item", "Present",
        [asyncResp,
         assemblyIndex](const boost::system::error_code& ec, const bool value) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec.value());
                messages::internalError(asyncResp->res);
                return;
            }

            if (!value)
            {
                nlohmann::json& array = asyncResp->res.jsonValue["Assemblies"];
                nlohmann::json& data = array.at(assemblyIndex);
                data["Status"]["State"] = resource::State::Absent;
            }
        });
}

/**
 * @brief Get properties for the assemblies associated to given chassis
 * @param[in] asyncResp - Shared pointer for asynchronous calls.
 * @param[in] chassisPath - Chassis the assemblies are associated with.
 * @param[in] assemblies - list of all the assemblies associated with the
 * chassis.
 * @return None.
 */
inline void getAssemblyProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath, const std::vector<std::string>& assemblies)
{
    BMCWEB_LOG_DEBUG("Get properties for assembly associated");

    const std::string& chassis =
        sdbusplus::message::object_path(chassisPath).filename();

    std::size_t assemblyIndex = 0;

    for (const auto& assembly : assemblies)
    {
        nlohmann::json& tempArray = asyncResp->res.jsonValue["Assemblies"];

        nlohmann::json::object_t item;
        item["@odata.type"] = "#Assembly.v1_3_0.AssemblyData";
        item["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/Assembly#/Assemblies/{}", chassis,
            std::to_string(assemblyIndex));
        item["MemberId"] = std::to_string(assemblyIndex);

        tempArray.emplace_back(item);

        tempArray.at(assemblyIndex)["Name"] =
            sdbusplus::message::object_path(assembly).filename();

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
            assembly, chassisAssemblyInterfaces,
            [asyncResp, assemblyIndex,
             assembly](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetObject& object) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DBUS response error : {}", ec.value());
                    messages::internalError(asyncResp->res);
                    return;
                }

                for (const auto& [serviceName, interfaceList] : object)
                {
                    for (const auto& interface : interfaceList)
                    {
                        if (interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Asset")
                        {
                            getAssemblyAsset(asyncResp, serviceName, assembly,
                                             assemblyIndex);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.LocationCode")
                        {
                            getAssemblyLocationCode(asyncResp, serviceName,
                                                    assembly, assemblyIndex);
                        }
                        else if (interface ==
                                 "xyz.openbmc_project.Inventory.Item")
                        {
                            getAssemblyPresence(asyncResp, serviceName,
                                                assembly, assemblyIndex);
                        }
                    }
                }
            });

        getLocationIndicatorActive(
            asyncResp, assembly, [asyncResp, assemblyIndex](bool asserted) {
                nlohmann::json& assemblyArray =
                    asyncResp->res.jsonValue["Assemblies"];
                nlohmann::json& assemblyData = assemblyArray.at(assemblyIndex);
                assemblyData["LocationIndicatorActive"] = asserted;
            });

        nlohmann::json& assemblyArray = asyncResp->res.jsonValue["Assemblies"];
        asyncResp->res.jsonValue["Assemblies@odata.count"] =
            assemblyArray.size();

        assemblyIndex++;
    }
}

/**
 * @brief Get chassis path with given chassis ID
 * @param[in] asyncResp - Shared pointer for asynchronous calls.
 * @param[in] chassisID - Chassis to which the assemblies are
 * associated.
 *
 * @return None.
 */
inline void handleChassisAssemblyGet(
    App& /*unused*/, const crow::Request& /*unused*/,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID)
{
    BMCWEB_LOG_DEBUG("Get chassis path");

    chassis_utils::getChassisAssembly(
        asyncResp, chassisID,
        [asyncResp,
         chassisID](const std::optional<std::string>& validChassisPath,
                    const std::vector<std::string>& assemblyList) {
            if (!validChassisPath)
            {
                BMCWEB_LOG_WARNING("Chassis not found");
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisID);
                return;
            }
            const std::string& chassisPath = *validChassisPath;

            asyncResp->res.jsonValue["@odata.type"] =
                "#Assembly.v1_3_0.Assembly";
            asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
                "/redfish/v1/Chassis/{}/Assembly", chassisID);
            asyncResp->res.jsonValue["Name"] = "Assembly Collection";
            asyncResp->res.jsonValue["Id"] = "Assembly";

            asyncResp->res.jsonValue["Assemblies"] = nlohmann::json::array();
            asyncResp->res.jsonValue["Assemblies@odata.count"] = 0;

            if (!assemblyList.empty())
            {
                getAssemblyProperties(asyncResp, chassisPath, assemblyList);
            }
        });
}

inline void startOrStopADCSensor(
    const bool start, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string method{"StartUnit"};
    if (!start)
    {
        method = "StopUnit";
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to start or stop ADCSensor:{}",
                                 ec.value());
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        },
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", method,
        "xyz.openbmc_project.adcsensor.service", "replace");
}

inline void afterGetDbusObjectDoBatteryCM(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& assembly, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }

    for (const auto& [serviceName, interfaceList] : object)
    {
        auto ifaceIt = std::ranges::find(
            interfaceList,
            "xyz.openbmc_project.State.Decorator.OperationalStatus");

        if (ifaceIt == interfaceList.end())
        {
            continue;
        }

        sdbusplus::asio::setProperty(
            *crow::connections::systemBus, serviceName, assembly,
            "xyz.openbmc_project.State.Decorator."
            "OperationalStatus",
            "Functional", true,
            [asyncResp, assembly](const boost::system::error_code& ec2) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR(
                        "Failed to set functional property on battery: {} ",
                        ec2.value());
                    messages::internalError(asyncResp->res);
                    return;
                }
                startOrStopADCSensor(true, asyncResp);
            });
        return;
    }

    BMCWEB_LOG_ERROR("No OperationalStatus interface on {}", assembly);
    messages::internalError(asyncResp->res);
}

inline void doBatteryCM(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& assembly, const bool readyToRemove)
{
    if (readyToRemove)
    {
        // Stop the adcsensor service so it doesn't monitor the battery
        startOrStopADCSensor(false, asyncResp);
        return;
    }

    // Find the service that has the OperationalStatus iface, set the
    // Functional property back to true, and then start the adcsensor service.
    std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.State.Decorator.OperationalStatus"};
    dbus::utility::getDbusObject(
        assembly, interfaces,
        std::bind_front(afterGetDbusObjectDoBatteryCM, asyncResp, assembly));
}

/**
 * @brief Set location indicator for the assemblies associated to given chassis
 * @param[in] req - The request data
 * @param[in] asyncResp - Shared pointer for asynchronous calls.
 * @param[in] chassisID - Chassis the assemblies are associated with.
 * @param[in] assemblies - list of all the assemblies associated with the
 * chassis.

 * @return None.
 */
inline void setAssemblyLocationIndicators(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::vector<std::string>& assemblies)
{
    BMCWEB_LOG_DEBUG(
        "Set LocationIndicatorActive for assembly associated to chassis = {}",
        chassisID);

    std::optional<std::vector<nlohmann::json>> assemblyData;
    if (!json_util::readJsonAction(req, asyncResp->res, "Assemblies",
                                   assemblyData))
    {
        return;
    }
    if (!assemblyData)
    {
        return;
    }

    std::vector<nlohmann::json> items = std::move(*assemblyData);
    std::map<std::string, bool> locationIndicatorActiveMap;
    std::map<std::string, nlohmann::json> oemIndicatorMap;

    for (auto& item : items)
    {
        std::optional<std::string> memberId;
        std::optional<bool> locationIndicatorActive;
        std::optional<nlohmann::json> oem;

        if (!json_util::readJson(
                item, asyncResp->res, "LocationIndicatorActive",
                locationIndicatorActive, "MemberId", memberId, "Oem", oem))
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
                    "Property Missing - MemberId must be included with LocationIndicatorActive ");
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

    std::size_t assemblyIndex = 0;
    for (const auto& assembly : assemblies)
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
            else
            {
                BMCWEB_LOG_WARNING(
                    "Property Unknown: ReadyToRemove on Assembly with MemberID: {}",
                    assemblyIndex);
                messages::propertyUnknown(asyncResp->res, "ReadyToRemove");
                return;
            }
        }
        assemblyIndex++;
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

    BMCWEB_LOG_DEBUG("Patch chassis path");

    chassis_utils::getChassisAssembly(
        asyncResp, chassisID,
        [req, asyncResp,
         chassisID](const std::optional<std::string>& validChassisPath,
                    const std::vector<std::string>& assemblyList) {
            if (!validChassisPath || assemblyList.empty())
            {
                BMCWEB_LOG_WARNING("Chassis not found");
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisID);
                return;
            }

            setAssemblyLocationIndicators(req, asyncResp, chassisID,
                                          assemblyList);
        });
}

/**
 * Systems derived class for delivering Assembly Schema.
 */
inline void requestRoutesAssembly(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
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
