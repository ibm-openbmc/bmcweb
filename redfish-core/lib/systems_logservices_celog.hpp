// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "log_services.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/error_log_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace redfish
{

inline void requestRoutesCELogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/CELog/")
        .privileges(redfish::privileges::getLogService)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& systemName) {
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
            asyncResp->res.jsonValue["@odata.id"] =
                boost::urls::format("/redfish/v1/Systems/{}/LogServices/CELog",
                                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
            asyncResp->res.jsonValue["@odata.type"] =
                "#LogService.v1_2_0.LogService";
            asyncResp->res.jsonValue["Name"] = "CE Log Service";
            asyncResp->res.jsonValue["Description"] = "System CE Log Service";
            asyncResp->res.jsonValue["Id"] = "CELog";
            asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";

            std::pair<std::string, std::string> redfishDateTimeOffset =
                redfish::time_utils::getDateTimeOffsetNow();

            asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
            asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                redfishDateTimeOffset.second;

            asyncResp->res.jsonValue["Entries"]["@odata.id"] =
                boost::urls::format(
                    "/redfish/v1/Systems/{}/LogServices/CELog/Entries",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
            asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"]
                                    ["target"] = boost::urls::format(
                "/redfish/v1/Systems/{}/LogServices/CELog/Actions/LogService.ClearLog",
                BMCWEB_REDFISH_SYSTEM_URI_NAME);
        });
}

inline void dBusCELogEntryCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Collections don't include the static data added by SubRoute
    // because it has a duplicate entry for members
    asyncResp->res.jsonValue["@odata.type"] =
        "#LogEntryCollection.LogEntryCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Systems/{}/LogServices/CELog/Entries",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["Name"] = "System Event Log Entries";
    asyncResp->res.jsonValue["Description"] =
        "Collection of System Event Log Entries";

    // DBus implementation of EventLog/Entries
    // Make call to Logging Service to find all log entry objects
    sdbusplus::message::object_path path("/xyz/openbmc_project/logging");
    dbus::utility::getManagedObjects(
        "xyz.openbmc_project.Logging", path,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::ManagedObjectType& resp) {
            boost::urls::url urlLogEntryPrefix = boost::urls::format(
                "/redfish/v1/Systems/{}/LogServices/CELog/Entries",
                BMCWEB_REDFISH_SYSTEM_URI_NAME);

            afterLogEntriesGetManagedObjects(asyncResp, urlLogEntryPrefix, true,
                                             ec, resp);
        });
}

inline void updateManagementSystemAckProperty(
    const std::optional<bool>& resolved,
    const std::optional<bool>& managementSystemAck,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& entryId)
{
    if (resolved.has_value())
    {
        setDbusProperty(asyncResp, "Resolved", "xyz.openbmc_project.Logging",
                        "/xyz/openbmc_project/logging/entry/" + entryId,
                        "xyz.openbmc_project.Logging.Entry", "Resolved",
                        *resolved);
    }

    if (managementSystemAck.has_value())
    {
        BMCWEB_LOG_DEBUG("Updated ManagementSystemAck Property");
        setDbusProperty(asyncResp, "ManagementSystemAck",
                        "xyz.openbmc_project.Logging",
                        "/xyz/openbmc_project/logging/entry/" + entryId,
                        "org.open_power.Logging.PEL.Entry",
                        "ManagementSystemAck", *managementSystemAck);
    }
}

inline void requestRoutesDBusCELogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/CELog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName) {
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
                dBusCELogEntryCollection(asyncResp);
            });
}

inline void dBusCELogEntryPatch(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& entryId)
{
    std::optional<bool> resolved;
    std::optional<bool> managementSystemAck;
    if (!json_util::readJsonPatch(
            req, asyncResp->res,                                   //
            "Resolved", resolved,                                  //
            "Oem/OpenBMC/ManagementSystemAck", managementSystemAck //
            ))
    {
        return;
    }

    error_log_utils::getHiddenPropertyValue(
        asyncResp, entryId,
        [resolved, managementSystemAck, asyncResp,
         entryId](bool hiddenPropVal) {
            if (!hiddenPropVal)
            {
                messages::resourceNotFound(asyncResp->res, "LogEntry", entryId);
                return;
            }
            updateManagementSystemAckProperty(resolved, managementSystemAck,
                                              asyncResp, entryId);
        });
}

inline void dBusCELogEntryDelete(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, std::string entryID)
{
    BMCWEB_LOG_DEBUG("Do delete single event entries.");
    dbus::utility::escapePathForDbus(entryID);

    error_log_utils::getHiddenPropertyValue(
        asyncResp, entryID, [asyncResp, entryID](bool hiddenPropVal) {
            if (!hiddenPropVal)
            {
                messages::resourceNotFound(asyncResp->res, "LogEntry", entryID);
                return;
            }
            dBusEventLogEntryDelete(asyncResp, entryID);
        });
}

inline void requestRoutesDBusCELogEntry(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/CELog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName, const std::string& entryId) {
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

                boost::urls::url urlLogEntryPrefix = boost::urls::format(
                    "/redfish/v1/Systems/{}/LogServices/CELog/Entries",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME);

                dBusEventLogEntryGet(asyncResp, entryId, urlLogEntryPrefix,
                                     true);
            });

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/CELog/Entries/<str>/")
        .privileges(
            redfish::privileges::
                patchLogEntrySubOverComputerSystemLogServiceCollectionLogServiceLogEntryCollection)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName, const std::string& entryId) {
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

                dBusCELogEntryPatch(req, asyncResp, entryId);
            });

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/CELog/Entries/<str>/")
        .privileges(
            redfish::privileges::
                deleteLogEntrySubOverComputerSystemLogServiceCollectionLogServiceLogEntryCollection)
        .methods(boost::beast::http::verb::delete_)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName, const std::string& param) {
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

                dBusCELogEntryDelete(asyncResp, param);
            });
}

inline void requestRoutesDBusCELogEntryDownloadPelJson(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/CELog/Entries/<str>/OemPelAttachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName, const std::string& param)

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

                std::string entryID = param;
                dbus::utility::escapePathForDbus(entryID);

                error_log_utils::getHiddenPropertyValue(
                    asyncResp, entryID,
                    [asyncResp, entryID](bool hiddenPropVal) {
                        if (!hiddenPropVal)
                        {
                            messages::resourceNotFound(asyncResp->res,
                                                       "LogEntry", entryID);
                            return;
                        }
                        displayOemPelAttachment(asyncResp, entryID);
                    });
            });
}

inline void requestRoutesDBusCELogEntryDownload(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/CELog/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleDBusEventLogEntryDownloadGet, std::ref(app), "System", true));
}

/**
 * DBusLogServiceActionsClear class supports POST method for ClearLog
 * action.
 */
inline void requestRoutesDBusCELogServiceActionsClear(App& app)
{
    /**
     * Function handles POST method request.
     * The Clear Log actions does not require any parameter.The action
     * deletes all entries found in the Entries collection for this Log
     * Service.
     */

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/CELog/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::
                        postLogServiceSubOverComputerSystemLogServiceCollection)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName) {
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

                dBusLogServiceActionsClear(asyncResp);
            });
}

} // namespace redfish
