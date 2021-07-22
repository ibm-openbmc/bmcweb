// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "assembly.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/resource.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/error_log_utils.hpp"
#include "utils/time_utils.hpp"

#include <asm-generic/errno.h>
#include <systemd/sd-bus.h>

#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace redfish
{
/****************************************************
 * Redfish HardwareIsolationLog interfaces
 ******************************************************/

constexpr std::array<std::string_view, 3> hwIsolationEntryIfaces = {
    "xyz.openbmc_project.HardwareIsolation.Entry",
    "xyz.openbmc_project.Association.Definitions",
    "xyz.openbmc_project.Time.EpochTime"};

using RedfishResourceDBusInterfaces = std::string;
using RedfishResourceCollectionUri = std::string;
using RedfishUriListType = std::unordered_map<RedfishResourceDBusInterfaces,
                                              RedfishResourceCollectionUri>;

static const RedfishUriListType redfishUriList = {
    {"xyz.openbmc_project.Inventory.Item.Cpu",
     "/redfish/v1/Systems/system/Processors"},
    {"xyz.openbmc_project.Inventory.Item.Dimm",
     "/redfish/v1/Systems/system/Memory"},
    {"xyz.openbmc_project.Inventory.Item.CpuCore",
     "/redfish/v1/Systems/system/Processors/<str>/SubProcessors"},
    {"xyz.openbmc_project.Inventory.Item.Chassis", "/redfish/v1/Chassis"},
    {"xyz.openbmc_project.Inventory.Item.Tpm",
     "/redfish/v1/Chassis/<str>/Assembly#/Assemblies"},
    {"xyz.openbmc_project.Inventory.Item.Board.Motherboard",
     "/redfish/v1/Chassis/<str>/Assembly#/Assemblies"}};

/**
 * @brief API Used to add the supported HardwareIsolation LogServices Members
 *
 * @param[in] req - The HardwareIsolation redfish request (unused now).
 * @param[in] asyncResp - The redfish response to return.
 *
 * @return The redfish response in the given buffer.
 */
inline void getSystemHardwareIsolationLogService(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/LogServices/HardwareIsolation",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["@odata.type"] = "#LogService.v1_2_0.LogService";
    asyncResp->res.jsonValue["Name"] = "Hardware Isolation LogService";
    asyncResp->res.jsonValue["Description"] =
        "Hardware Isolation LogService for system owned devices";
    asyncResp->res.jsonValue["Id"] = "HardwareIsolation";

    asyncResp->res.jsonValue["Entries"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/LogServices/HardwareIsolation/Entries",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);

    asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Systems/{}/LogServices/HardwareIsolation/Actions/LogService.ClearLog",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
}

/**
 * @brief Workaround to handle DCM (Dual-Chip Module) package for Redfish
 *
 * This API will make sure processor modeled as dual chip module, If yes then,
 * replace the redfish processor id as "dcmN-cpuN" because redfish currently
 * does not support chip module concept.
 *
 * @param[in] dbusObjPath - The D-Bus object path to return the object instance
 *
 * @return the object instance with it parent instance id if the given object
 *         is a processor else the object instance alone.
 */
inline std::string getIsolatedHwItemId(
    const sdbusplus::message::object_path& dbusObjPath)
{
    std::string isolatedHwItemId;

    if ((dbusObjPath.filename().find("cpu") != std::string::npos) &&
        (dbusObjPath.parent_path().filename().find("dcm") != std::string::npos))
    {
        isolatedHwItemId =
            std::format("{}-{}", dbusObjPath.parent_path().filename(),
                        dbusObjPath.filename());
    }
    else
    {
        isolatedHwItemId = dbusObjPath.filename();
    }
    return isolatedHwItemId;
}

/**
 * @brief API used to get redfish uri of the given dbus object and fill into
 *        "OriginOfCondition" property of LogEntry schema.
 *
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] dbusObjPath - The DBus object path which represents redfishUri.
 * @param[in] entryJsonIdx - The json entry index to add isolated hardware
 *                            details in the appropriate entry json object.
 *
 * @return The redfish response with "OriginOfCondition" property of
 *         LogEntry schema if success else return the error
 */
inline void getRedfishUriByDbusObjPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& dbusObjPath,
    const size_t entryJsonIdx)
{
    dbus::utility::getDbusObject( //
        dbusObjPath.str, std::array<std::string_view, 0>{},
        [asyncResp, dbusObjPath,
         entryJsonIdx](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error [{} : {}] when tried to get the RedfishURI of isolated hareware: {}",
                    ec.value(), ec.message(), dbusObjPath.str);
                messages::internalError(asyncResp->res);
                return;
            }

            RedfishUriListType::const_iterator redfishUriIt;
            for (const auto& service : objType)
            {
                for (const auto& interface : service.second)
                {
                    redfishUriIt = redfishUriList.find(interface);
                    if (redfishUriIt != redfishUriList.end())
                    {
                        // Found the Redfish URI of the
                        // isolated hardware unit.
                        break;
                    }
                }
                if (redfishUriIt != redfishUriList.end())
                {
                    // No need to check in the next
                    // service interface list
                    break;
                }
            }

            if (redfishUriIt == redfishUriList.end())
            {
                BMCWEB_LOG_ERROR(
                    "The object[{}] interface is not found in the Redfish URI list. Please add the respective D-Bus interface name",
                    dbusObjPath.str);
                messages::internalError(asyncResp->res);
                return;
            }

            // Fill the isolated hardware object id along
            // with the Redfish URI
            std::string redfishUri =
                std::format("{}/{}", redfishUriIt->second,
                            getIsolatedHwItemId(dbusObjPath));

            // Make sure whether no need to fill the
            // parent object id in the isolated hardware
            // Redfish URI.
            const std::string uriIdPattern{"<str>"};
            size_t uriIdPos = redfishUri.rfind(uriIdPattern);
            if (uriIdPos == std::string::npos)
            {
                if (entryJsonIdx > 0)
                {
                    asyncResp->res
                        .jsonValue["Members"][entryJsonIdx - 1]["Links"]
                                  ["OriginOfCondition"]["@odata.id"] =
                        redfishUri;
                }
                else
                {
                    asyncResp->res
                        .jsonValue["Links"]["OriginOfCondition"]["@odata.id"] =
                        redfishUri;
                }
                return;
            }
            bool isChassisAssemblyUri = false;
            std::string::size_type assemblyStartPos =
                redfishUri.rfind("/Assembly#/Assemblies");
            if (assemblyStartPos != std::string::npos)
            {
                // Redfish URI using path segment like
                // DBus object path so using object_path
                // type
                if (sdbusplus::message::object_path(
                        redfishUri.substr(0, assemblyStartPos))
                        .parent_path()
                        .filename() != "Chassis")
                {
                    // Currently, bmcweb supporting only
                    // chassis assembly uri so return
                    // error if unsupported assembly uri
                    // added in the redfishUriList.
                    BMCWEB_LOG_ERROR(
                        "Unsupported Assembly URI [{}] to fill in the OriginOfCondition. Please add support in the bmcweb",
                        redfishUri);
                    messages::internalError(asyncResp->res);
                    return;
                }
                isChassisAssemblyUri = true;
            }
            // Fill the all parents Redfish URI id.
            // For example, the processors id for the
            // core.
            // "/redfish/v1/Systems/system/Processors/<str>/SubProcessors/core0"
            std::vector<std::pair<RedfishResourceDBusInterfaces, size_t>>
                ancestorsIfaces;
            while (uriIdPos != std::string::npos)
            {
                std::string parentRedfishUri =
                    redfishUri.substr(0, uriIdPos - 1);
                RedfishUriListType::const_iterator parentRedfishUriIt =
                    std::ranges::find_if(
                        redfishUriList, [parentRedfishUri](const auto& ele) {
                            return parentRedfishUri == ele.second;
                        });

                if (parentRedfishUriIt == redfishUriList.end())
                {
                    BMCWEB_LOG_ERROR(
                        "Failed to fill Links:OriginOfCondition because unable to get parent Redfish URI [{}] DBus interface for the identified Redfish URI: {} of the given DBus object path: {}",
                        parentRedfishUri, redfishUri, dbusObjPath.str);
                    messages::internalError(asyncResp->res);
                    return;
                }
                ancestorsIfaces.emplace_back(parentRedfishUriIt->first,
                                             uriIdPos);
                size_t tempUriIdPos = uriIdPos - uriIdPattern.length();
                uriIdPos = redfishUri.rfind(uriIdPattern, tempUriIdPos);
            }

            // GetAncestors only accepts "as" for the
            // interface list
            std::vector<std::string_view> ancestorsIfaceViews;
            ancestorsIfaceViews.reserve(ancestorsIfaces.size());

            std::transform(
                ancestorsIfaces.begin(), ancestorsIfaces.end(),
                std::back_inserter(ancestorsIfaceViews),
                [](const std::pair<RedfishResourceDBusInterfaces, size_t>& p) {
                    return std::string_view(p.first);
                });
            dbus::utility::getAncestors(
                dbusObjPath.str,
                std::span<const std::string_view>{ancestorsIfaceViews},
                [asyncResp, dbusObjPath, entryJsonIdx, redfishUri, uriIdPattern,
                 ancestorsIfaces, isChassisAssemblyUri](
                    const boost::system::error_code& ec1,
                    const dbus::utility::MapperGetSubTreeResponse&
                        ancestors) mutable {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error [{} : {}] when tried to fill the parent objects id in the RedfishURI: {} of the isolated hareware: {}",
                            ec1.value(), ec1.message(), redfishUri,
                            dbusObjPath.str);

                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // tuple: assembly parent service name, object path, and
                    // interface
                    std::tuple<std::string, sdbusplus::message::object_path,
                               std::string>
                        assemblyParent;

                    for (const auto& ancestorIface : ancestorsIfaces)
                    {
                        bool foundAncestor = false;
                        for (const auto& obj : ancestors)
                        {
                            for (const auto& service : obj.second)
                            {
                                auto interfaceIter = std::ranges::find(
                                    service.second, ancestorIface.first);
                                if (interfaceIter != service.second.end())
                                {
                                    foundAncestor = true;
                                    redfishUri.replace(
                                        ancestorIface.second,
                                        uriIdPattern.length(),
                                        getIsolatedHwItemId(
                                            sdbusplus::message::object_path(
                                                obj.first)));

                                    if (isChassisAssemblyUri &&
                                        ancestorIface.first ==
                                            "xyz.openbmc_project.Inventory.Item.Chassis")
                                    {
                                        assemblyParent = std::make_tuple(
                                            service.first,
                                            sdbusplus::message::object_path(
                                                obj.first),
                                            ancestorIface.first);
                                    }
                                    break;
                                }
                            }
                            if (foundAncestor)
                            {
                                break;
                            }
                        }

                        if (!foundAncestor)
                        {
                            BMCWEB_LOG_ERROR(
                                "Failed to fill Links:OriginOfCondition because unable to get parent DBus path for the identified parent interface : {} of the given DBus object path: {}",
                                ancestorIface.first, dbusObjPath.str);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                    }

                    if (entryJsonIdx > 0)
                    {
                        asyncResp->res
                            .jsonValue["Members"][entryJsonIdx - 1]["Links"]
                                      ["OriginOfCondition"]["@odata.id"] =
                            redfishUri;

                        if (isChassisAssemblyUri)
                        {
                            auto uriPropPath = "/Members"_json_pointer;
                            uriPropPath /= entryJsonIdx - 1;
                            uriPropPath /= "Links/OriginOfCondition/@odata.id";

                            assembly::fillWithAssemblyId(
                                asyncResp, std::get<0>(assemblyParent),
                                std::get<1>(assemblyParent),
                                std::get<2>(assemblyParent), uriPropPath,
                                dbusObjPath, redfishUri);
                        }
                    }
                    else
                    {
                        asyncResp->res.jsonValue["Links"]["OriginOfCondition"]
                                                ["@odata.id"] = redfishUri;

                        if (isChassisAssemblyUri)
                        {
                            auto uriPropPath =
                                "/Links/OriginOfCondition/@odata.id"_json_pointer;

                            assembly::fillWithAssemblyId(
                                asyncResp, std::get<0>(assemblyParent),
                                std::get<1>(assemblyParent),
                                std::get<2>(assemblyParent), uriPropPath,
                                dbusObjPath, redfishUri);
                        }
                    }
                });
        });
}

/**
 * @brief API used to load the message and Message Args for the HW Isolation
 * Entries
 *
 * @param[in] ec - error code from the dbus call.
 * @param[in] prettyName - The prettyname fetched from the dbus object.
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] path - D-bus object path to find the pretty name
 * @param[in] entryJsonIdx - The json entry index to add isolated hardware
 *                            details in the appropriate entry json object.
 * @param[in] guardType - guardType value to be used in the message
 *
 * @return The redfish response with "Message" property of
 *         Hw Isolation Entry schema if success else return the error
 */
static void loadHwIsolationMessage(
    const boost::system::error_code& ec, const std::string& prettyName,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, const size_t entryJsonIdx,
    const std::string& guardType)
{
    // Determine base path based on entryJsonIdx
    nlohmann::json::json_pointer msgPropPath;
    nlohmann::json::json_pointer msgArgPropPath;
    if (entryJsonIdx > 0)
    {
        msgPropPath = nlohmann::json::json_pointer(
            "/Members/" + std::to_string(entryJsonIdx - 1) + "/Message");
        msgArgPropPath = nlohmann::json::json_pointer(
            "/Members/" + std::to_string(entryJsonIdx - 1) + "/MessageArgs");
    }
    else
    {
        msgPropPath = nlohmann::json::json_pointer("/Message");
        msgArgPropPath = nlohmann::json::json_pointer("/MessageArgs");
    }
    std::vector<std::string_view> messageArgs;
    messageArgs.push_back(guardType);
    if (ec || prettyName.empty())
    {
        messageArgs.push_back(path);
    }
    else
    {
        messageArgs.push_back(prettyName);
    }
    const redfish::registries::Message* msgReg =
        registries::getMessage("OpenBMC.0.6.GuardRecord");
    if (msgReg == nullptr)
    {
        BMCWEB_LOG_ERROR(
            "Failed to get the GuardRecord message registry to add in the condition");
        messages::internalError(asyncResp->res);
        return;
    }
    std::string msg =
        redfish::registries::fillMessageArgs(messageArgs, msgReg->message);
    if (msg.empty())
    {
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue[msgPropPath] = msg;
    asyncResp->res.jsonValue[msgArgPropPath] = messageArgs;
}

/**
 * @brief Read the Pretty Name property using the dbus call and load the message
 * property
 *
 * @param[in,out] asyncResp Async response object
 * @param[in]   path        D-bus object path to find the pretty name
 * @param[in]   services    List of services to exporting the D-bus object path
 * @param[in] entryJsonIdx  The json entry index to add isolated hardware
 *                          details in the appropriate entry json object.
 * @param[in]   guardType   The guardtype value as a string. To be used in
 *                          the redfish message property
 *
 * @return void
 */
inline void updateHwIsolationMessage(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, const dbus::utility::MapperServiceMap& services,
    const size_t entryJsonIdx, const std::string& guardType)
{
    // Ensure we only got one service back
    if (services.size() != 1)
    {
        BMCWEB_LOG_ERROR("Invalid Service Size {}", services.size());
        for (const auto& service : services)
        {
            BMCWEB_LOG_ERROR("Invalid Service Name: {}", service.first);
        }
        if (asyncResp)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }
    dbus::utility::getProperty<std::string>(
        *crow::connections::systemBus, services[0].first, path,
        "xyz.openbmc_project.Inventory.Item", "PrettyName",
        [asyncResp, path, entryJsonIdx,
         guardType](const boost::system::error_code& ec,
                    const std::string& prettyName) {
            loadHwIsolationMessage(ec, prettyName, asyncResp, path,
                                   entryJsonIdx, guardType);
        });
}

/**
 * @brief API used to get "PrettyName" by using the given dbus object path
 *        and fill into "Message" property of LogEntry schema.
 *
 * @param[in] asyncResp .   The redfish response to return.
 * @param[in] dbusObjPath   The DBus object path which represents redfishUri.
 * @param[in] entryJsonIdx  The json entry index to add isolated hardware
 *                            details in the appropriate entry json object.
 * @param[in]   guardType   The guardtype value as a string. To be used in
 *                          the redfish message property
 * @return void
 */

inline void getPrettyNameByDbusObjPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& dbusObjPath,
    const size_t entryJsonIdx, const std::string& guardType)
{
    constexpr std::array<std::string_view, 1> interface = {
        "xyz.openbmc_project.Inventory.Item"};
    dbus::utility::getDbusObject(
        dbusObjPath.str, interface,
        [asyncResp, dbusObjPath, entryJsonIdx,
         guardType](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& objType) mutable {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error [{} : {}] when tried to get the dbus name of isolated hareware: {}",
                    ec.value(), ec.message(), dbusObjPath.str);
                messages::internalError(asyncResp->res);
                return;
            }

            if (objType.size() > 1)
            {
                BMCWEB_LOG_ERROR(
                    "More than one dbus service implemented the xyz.openbmc_project.Inventory.Item interface to get the PrettyName");
                messages::internalError(asyncResp->res);
                return;
            }

            if (objType[0].first.empty())
            {
                BMCWEB_LOG_ERROR(
                    "The retrieved dbus name is empty for the given dbus object: {}",
                    dbusObjPath.str);
                messages::internalError(asyncResp->res);
                return;
            }

            updateHwIsolationMessage(asyncResp, dbusObjPath.str, objType,
                                     entryJsonIdx, guardType);
        });
}

/**
 * @brief API used to fill the isolated hardware details into LogEntry schema
 *        by using the given isolated dbus object which is present in
 *        xyz.openbmc_project.Association.Definitions::Associations of the
 *        HardwareIsolation dbus entry object.
 *
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] dbusObjPath - The DBus object path which represents redfishUri.
 * @param[in] entryJsonIdx - The json entry index to add isolated hardware
 *                            details in the appropriate entry json object.
 *
 * @return The redfish response with appropriate redfish properties of the
 *         isolated hardware details into LogEntry schema if success else
 *         nothing in redfish response.
 */
inline void fillIsolatedHwDetailsByObjPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& dbusObjPath,
    const size_t entryJsonIdx, const std::string& guardType)
{
    // Fill Redfish uri of isolated hardware into "OriginOfCondition"
    if (dbusObjPath.filename().find("unit") != std::string::npos)
    {
        // If Isolated Hardware object name contain "unit" then that unit
        // is not modelled in inventory and redfish so the "OriginOfCondition"
        // should filled with it's parent (aka FRU of unit) path.
        getRedfishUriByDbusObjPath(asyncResp, dbusObjPath.parent_path(),
                                   entryJsonIdx);
    }
    else
    {
        getRedfishUriByDbusObjPath(asyncResp, dbusObjPath, entryJsonIdx);
    }

    // Fill PrettyName of isolated hardware into "Message"
    getPrettyNameByDbusObjPath(asyncResp, dbusObjPath, entryJsonIdx, guardType);
}

/**
 * @brief API used to fill isolated hardware details into LogEntry schema
 *        by using the given isolated dbus object.
 *
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] entryJsonIdx - The json entry index to add isolated hardware
 *                            details. If passing less than or equal 0 then,
 *                            it will assume the given asyncResp jsonValue as
 *                            a single entry json object. If passing greater
 *                            than 0 then, it will assume the given asyncResp
 *                            jsonValue contains "Members" to fill in the
 *                            appropriate entry json object.
 * @param[in] dbusObjIt - The DBus object which contains isolated hardware
                         details.
 *
 * @return The redfish response with appropriate redfish properties of the
 *         isolated hardware details into LogEntry schema if success else
 *         failure response.
 */
inline void fillSystemHardwareIsolationLogEntry(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const size_t entryJsonIdx,
    const dbus::utility::ManagedObjectType::const_iterator& dbusObjIt)
{
    nlohmann::json& entryJson =
        (entryJsonIdx > 0
             ? asyncResp->res.jsonValue["Members"][entryJsonIdx - 1]
             : asyncResp->res.jsonValue);

    std::string guardType;
    // We need the severity details before getting the associations
    // to fill the message details.

    for (const auto& interface : dbusObjIt->second)
    {
        if (interface.first == "xyz.openbmc_project.HardwareIsolation.Entry")
        {
            for (const auto& property : interface.second)
            {
                if (property.first == "Severity")
                {
                    const std::string* severity =
                        std::get_if<std::string>(&property.second);
                    if (severity == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Failed to get the Severity from object: {}",
                            dbusObjIt->first.str);
                        messages::internalError(asyncResp->res);
                        break;
                    }

                    const std::string& severityString = *severity;
                    guardType =
                        severityString.substr(severityString.rfind('.') + 1);
                    entryJson["Severity"] = guardType;
                    // Manual and Spare guard type map to severity "OK"
                    if (severityString ==
                            "xyz.openbmc_project.HardwareIsolation.Entry.Type.Manual" ||
                        severityString ==
                            "xyz.openbmc_project.HardwareIsolation.Entry.Type.Spare")
                    {
                        entryJson["Severity"] = resource::Health::OK;
                    }
                }
            }
            break;
        }
    }

    for (const auto& interface : dbusObjIt->second)
    {
        if (interface.first == "xyz.openbmc_project.Time.EpochTime")
        {
            for (const auto& property : interface.second)
            {
                if (property.first == "Elapsed")
                {
                    const uint64_t* elapsdTime =
                        std::get_if<uint64_t>(&property.second);
                    if (elapsdTime == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Failed to get the Elapsed time from object: {}",
                            dbusObjIt->first.str);
                        messages::internalError(asyncResp->res);
                        break;
                    }
                    entryJson["Created"] =
                        redfish::time_utils::getDateTimeUint((*elapsdTime));
                }
            }
        }
        else if (interface.first ==
                 "xyz.openbmc_project.Association.Definitions")
        {
            for (const auto& property : interface.second)
            {
                if (property.first == "Associations")
                {
                    using AssociationsValType = std::vector<
                        std::tuple<std::string, std::string, std::string>>;
                    const AssociationsValType* associations =
                        std::get_if<AssociationsValType>(&property.second);
                    if (associations == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Failed to get the Associations from object: {}",
                            dbusObjIt->first.str);
                        messages::internalError(asyncResp->res);
                        break;
                    }
                    for (const auto& assoc : *associations)
                    {
                        if (std::get<0>(assoc) == "isolated_hw")
                        {
                            fillIsolatedHwDetailsByObjPath(
                                asyncResp,
                                sdbusplus::message::object_path(
                                    std::get<2>(assoc)),
                                entryJsonIdx, guardType);
                        }
                        else if (std::get<0>(assoc) == "isolated_hw_errorlog")
                        {
                            sdbusplus::message::object_path errPath =
                                std::get<2>(assoc);

                            std::string entryID = errPath.filename();

                            auto updateAdditionalDataURI = [asyncResp,
                                                            entryJsonIdx,
                                                            entryID](
                                                               bool hidden) {
                                nlohmann::json& entryJsonToupdateURI =
                                    (entryJsonIdx > 0
                                         ? asyncResp->res
                                               .jsonValue["Members"]
                                                         [entryJsonIdx - 1]
                                         : asyncResp->res.jsonValue);
                                std::string logPath = "EventLog";

                                if (hidden)
                                {
                                    logPath = "CELog";
                                }
                                entryJsonToupdateURI["AdditionalDataURI"] =
                                    boost::urls::format(
                                        "/redfish/v1/Systems/{}/LogServices/{}/Entries/{}/attachment",
                                        BMCWEB_REDFISH_SYSTEM_URI_NAME, logPath,
                                        entryID);
                            };
                            redfish::error_log_utils::getHiddenPropertyValue(
                                asyncResp, entryID, updateAdditionalDataURI);
                        }
                    }
                }
            }
        }
    }

    entryJson["@odata.type"] = "#LogEntry.v1_9_0.LogEntry";
    entryJson["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/LogServices/HardwareIsolation/Entries/{}",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, dbusObjIt->first.filename());
    entryJson["Id"] = dbusObjIt->first.filename();
    entryJson["MessageId"] = "OpenBMC.0.6.GuardRecord";
    entryJson["Name"] = "Hardware Isolation Entry";
    entryJson["EntryType"] = "Event";
}

/**
 * @brief API Used to add the supported HardwareIsolation LogEntry Entries id
 *
 * @param[in] req - The HardwareIsolation redfish request (unused now).
 * @param[in] asyncResp - The redfish response to return.
 *
 * @return The redfish response in the given buffer.
 *
 * @note This function will return the available entries dbus object which are
 *       created by HardwareIsolation manager.
 */
inline void getSystemHardwareIsolationLogEntryCollection(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    auto getManagedObjectsHandler = [asyncResp](
                                        const boost::system::error_code& ec,
                                        const dbus::utility::ManagedObjectType&
                                            mgtObjs) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "DBUS response error [{} : {}] when tried to get the HardwareIsolation managed objects",
                ec.value(), ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        nlohmann::json& entriesArray = asyncResp->res.jsonValue["Members"];
        entriesArray = nlohmann::json::array();

        for (auto dbusObjIt = mgtObjs.begin(); dbusObjIt != mgtObjs.end();
             dbusObjIt++)
        {
            bool found = false;
            for (const auto& interface : dbusObjIt->second)
            {
                if (interface.first ==
                    "xyz.openbmc_project.HardwareIsolation.Entry")
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                continue; // The retrieved object is not hardware isolation
                          // entry
            }

            entriesArray.push_back(nlohmann::json::object());

            fillSystemHardwareIsolationLogEntry(asyncResp, entriesArray.size(),
                                                dbusObjIt);
        }

        asyncResp->res.jsonValue["Members@odata.count"] = entriesArray.size();

        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/Systems/{}/LogServices/HardwareIsolation/Entries",
            BMCWEB_REDFISH_SYSTEM_URI_NAME);
        asyncResp->res.jsonValue["Name"] = "Hardware Isolation Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of System Hardware Isolation Entries";
    };

    // Get the DBus name of HardwareIsolation service
    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/hardware_isolation",
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.HardwareIsolation.Create"},
        [asyncResp, getManagedObjectsHandler](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error [{} : {}] when tried to get the HardwareIsolation dbus name",
                    ec.value(), ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            if (objType.size() > 1)
            {
                BMCWEB_LOG_ERROR(
                    "More than one dbus service implemented the HardwareIsolation service");
                messages::internalError(asyncResp->res);
                return;
            }

            if (objType[0].first.empty())
            {
                BMCWEB_LOG_ERROR(
                    "The retrieved HardwareIsolation dbus name is empty");
                messages::internalError(asyncResp->res);
                return;
            }

            // Fill the Redfish LogEntry schema for the retrieved
            // HardwareIsolation entries
            sdbusplus::message::object_path path(
                "/xyz/openbmc_project/hardware_isolation");
            dbus::utility::getManagedObjects(objType[0].first, path,
                                             getManagedObjectsHandler);
        });
}

/**
 * @brief API Used to fill LogEntry schema by using the HardwareIsolation dbus
 *        entry object which will get by using the given entry id in redfish
 *        uri.
 *
 * @param[in] req - The HardwareIsolation redfish request (unused now).
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] entryId - The entry id of HardwareIsolation entries to retrieve
 *                      the corresponding isolated hardware details.
 *
 * @return The redfish response in the given buffer with LogEntry schema
 *         members if success else will error.
 */
inline void getSystemHardwareIsolationLogEntryById(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& entryId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    sdbusplus::message::object_path entryObjPath(std::format(
        "/xyz/openbmc_project/hardware_isolation/entry/{}", entryId));

    auto getManagedObjectsRespHandler = [asyncResp, entryObjPath](
                                            const boost::system::error_code& ec,
                                            const dbus::utility::
                                                ManagedObjectType& mgtObjs) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "DBUS response error [{}:{}] when tried to get the HardwareIsolation managed objects",
                ec.value(), ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        bool entryIsPresent = false;
        for (auto dbusObjIt = mgtObjs.begin(); dbusObjIt != mgtObjs.end();
             dbusObjIt++)
        {
            if (dbusObjIt->first == entryObjPath)
            {
                entryIsPresent = true;
                fillSystemHardwareIsolationLogEntry(asyncResp, 0, dbusObjIt);
                break;
            }
        }

        if (!entryIsPresent)
        {
            messages::resourceNotFound(asyncResp->res, "Entry",
                                       entryObjPath.filename());
            return;
        }
    };

    auto getObjectRespHandler = [asyncResp, entryId, entryObjPath,
                                 getManagedObjectsRespHandler](
                                    const boost::system::error_code& ec,
                                    const dbus::utility::MapperGetObject&
                                        objType) {
        if (ec || objType.empty())
        {
            BMCWEB_LOG_ERROR(
                "DBUS response error [{} : {}] when tried to get the HardwareIsolation dbus name the given object path: {}",
                ec.value(), ec.message(), entryObjPath.str);

            if (ec.value() == EBADR)
            {
                messages::resourceNotFound(asyncResp->res, "Entry", entryId);
            }
            else
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        if (objType.size() > 1)
        {
            BMCWEB_LOG_ERROR(
                "More than one dbus service implemented the HardwareIsolation service");
            messages::internalError(asyncResp->res);
            return;
        }

        if (objType[0].first.empty())
        {
            BMCWEB_LOG_ERROR(
                "The retrieved HardwareIsolation dbus name is empty");
            messages::internalError(asyncResp->res);
            return;
        }

        // Fill the Redfish LogEntry schema for the identified entry dbus object
        sdbusplus::message::object_path path(
            "/xyz/openbmc_project/hardware_isolation");
        dbus::utility::getManagedObjects(objType[0].first, path,
                                         getManagedObjectsRespHandler);
    };

    // Make sure the given entry id is present in hardware isolation
    // dbus entries and get the DBus name of that entry to fill LogEntry
    dbus::utility::getDbusObject(entryObjPath.str, hwIsolationEntryIfaces,
                                 getObjectRespHandler);
}

/**
 * @brief API Used to deisolate the given HardwareIsolation entry.
 *
 * @param[in] req - The HardwareIsolation redfish request (unused now).
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] entryId - The entry id of HardwareIsolation entries to deisolate.
 *
 * @return The redfish response in the given buffer.
 */
inline void deleteSystemHardwareIsolationLogEntryById(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& entryId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    sdbusplus::message::object_path entryObjPath(
        std::string("/xyz/openbmc_project/hardware_isolation/entry") + "/" +
        entryId);

    // Make sure the given entry id is present in hardware isolation
    // entries and get the DBus name of that entry
    dbus::utility::getDbusObject(
        entryObjPath.str, hwIsolationEntryIfaces,
        [asyncResp, entryId,
         entryObjPath](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error [{} : {}] when tried to get the HardwareIsolation dbus name the given object path: ",
                    ec.value(), ec.message(), entryObjPath.str);
                if (ec.value() == EBADR)
                {
                    messages::resourceNotFound(asyncResp->res, "Entry",
                                               entryId);
                }
                else
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            if (objType.size() > 1)
            {
                BMCWEB_LOG_ERROR(
                    "More than one dbus service implemented the HardwareIsolation service");
                messages::internalError(asyncResp->res);
                return;
            }

            if (objType[0].first.empty())
            {
                BMCWEB_LOG_ERROR(
                    "The retrieved HardwareIsolation dbus name is empty");
                messages::internalError(asyncResp->res);
                return;
            }

            // Delete the respective dbus entry object
            crow::connections::systemBus->async_method_call(
                [asyncResp,
                 entryObjPath](const boost::system::error_code& ec1,
                               const sdbusplus::message::message& msg) {
                    if (!ec1)
                    {
                        messages::success(asyncResp->res);
                        return;
                    }

                    BMCWEB_LOG_ERROR(
                        "DBUS response error [{} : {}] when tried to delete the given object path: ",
                        ec1.value(), ec1.message(), entryObjPath.str);

                    const sd_bus_error* dbusError = msg.get_error();

                    if (dbusError == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    BMCWEB_LOG_ERROR("DBus ErrorName: {} ErrorMsg: {}",
                                     dbusError->name, dbusError->message);

                    if (std::string_view(
                            "xyz.openbmc_project.Common.Error.NotAllowed") ==
                        dbusError->name)
                    {
                        messages::chassisPowerStateOffRequired(asyncResp->res,
                                                               "chassis");
                    }
                    else if (
                        std::string_view(
                            "xyz.openbmc_project.Common.Error.InsufficientPermission") ==
                        dbusError->name)
                    {
                        messages::resourceCannotBeDeleted(asyncResp->res);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR(
                            "DBus Error is unsupported so returning as Internal Error");
                        messages::internalError(asyncResp->res);
                    }
                    return;
                },
                objType[0].first, entryObjPath.str,
                "xyz.openbmc_project.Object.Delete", "Delete");
        });
}
/**
 * @brief API Used to deisolate the all HardwareIsolation entries.
 *
 * @param[in] req - The HardwareIsolation redfish request (unused now).
 * @param[in] asyncResp - The redfish response to return.
 *
 * @return The redfish response in the given buffer.
 */
inline void postSystemHardwareIsolationLogServiceClearLog(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    // Get the DBus name of HardwareIsolation service
    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/hardware_isolation",
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Collection.DeleteAll"},
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error [{} : {}] when tried to get the HardwareIsolation dbus name",
                    ec.value(), ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            if (objType.size() > 1)
            {
                BMCWEB_LOG_ERROR(
                    "More than one dbus service implemented the HardwareIsolation service");
                messages::internalError(asyncResp->res);
                return;
            }

            if (objType[0].first.empty())
            {
                BMCWEB_LOG_ERROR(
                    "The retrieved HardwareIsolation dbus name is empty");
                messages::internalError(asyncResp->res);
                return;
            }

            // Delete all HardwareIsolation entries
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code& ec1) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error [{} : {}] when tried to delete all HardwareIsolation entries",
                            ec1.value(), ec1.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    messages::success(asyncResp->res);
                },
                objType[0].first, "/xyz/openbmc_project/hardware_isolation",
                "xyz.openbmc_project.Collection.DeleteAll", "DeleteAll");
        });
}
/**
 * @brief API used to route the handler for HardwareIsolation Redfish
 *        LogServices URI
 *
 * @param[in] app - Crow app on which Redfish will initialize
 *
 * @return The appropriate redfish response for the given redfish request.
 */
inline void requestRoutesSystemHardwareIsolationLogService(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/HardwareIsolation/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            getSystemHardwareIsolationLogService, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/HardwareIsolation/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            getSystemHardwareIsolationLogEntryCollection, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/HardwareIsolation/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            getSystemHardwareIsolationLogEntryById, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/HardwareIsolation/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            deleteSystemHardwareIsolationLogEntryById, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/HardwareIsolation/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::
                        postLogServiceSubOverComputerSystemLogServiceCollection)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            postSystemHardwareIsolationLogServiceClearLog, std::ref(app)));
}

} // namespace redfish
