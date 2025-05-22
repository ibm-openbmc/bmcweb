// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#pragma once

#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <boost_formatters.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace redfish
{
namespace error_log_utils
{

static void getHiddenPropertyValue(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& entryId, std::function<void(bool hidden)>&& callback)
{
    // Explicit move here to avoid clang tidy warning
    auto movedCallback = std::move(callback);

    dbus::utility::getProperty<bool>(
        *crow::connections::systemBus, "xyz.openbmc_project.Logging",
        "/xyz/openbmc_project/logging/entry/" + entryId,
        "org.open_power.Logging.PEL.Entry", "Hidden",
        [movedCallback = std::move(movedCallback), asyncResp,
         entryId](const boost::system::error_code& ec, bool hidden) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to get DBUS property 'Hidden' for entry {}: {}",
                    entryId, ec);
                redfish::messages::internalError(asyncResp->res);
                return;
            }
            movedCallback(hidden);
        });
}

/*
 * @brief The helper API to set the Redfish error log URI in the given
 *        Redfish property JSON path based on the Hidden property
 *        which will be present in the given error log D-Bus object.
 *
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] errorLogObjPath - The error log D-Bus object path.
 * @param[in] errorLogPropPath - The Redfish property json path to fill URI.
 * @param[in] isLink - The boolean to add URI as a Redfish link.
 *
 * @return NULL
 *
 * @note The "isLink" parameter is used to add the URI as a link (i.e with
 *       "@odata.id"). If passed as "false" then, the suffix will be added
 *       as "/attachment" along with the URI.
 *
 *       This API won't fill the given "errorLogPropPath" property if unable
 *       to process the given error log D-Bus object since the error log
 *       might delete by the user via Redfish but, we should not throw
 *       internal error in that case, just log trace and return.
 */
inline void setErrorLogUri(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& errorLogObjPath,
    const nlohmann::json::json_pointer& errorLogPropPath, const bool isLink)
{
    std::string entryID = errorLogObjPath.filename();
    auto updateErrorLogPath =
        [asyncResp, entryID, errorLogPropPath, isLink](bool hidden) {
            std::string logPath = "EventLog";
            if (hidden)
            {
                logPath = "CELog";
            }
            std::string linkAttachment;
            if (!isLink)
            {
                linkAttachment = "/attachment";
            }
            asyncResp->res.jsonValue[errorLogPropPath]["@odata.id"] =
                boost::urls::format(
                    "/redfish/v1/Systems/{}/LogServices/{}/Entries/{}{}",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME, logPath, entryID,
                    linkAttachment);
        };
    getHiddenPropertyValue(asyncResp, entryID, updateErrorLogPath);
}

} // namespace error_log_utils
} // namespace redfish
