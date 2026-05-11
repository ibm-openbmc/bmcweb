// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"

#include <asm-generic/errno.h>

#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/property.hpp>

#include <memory>

namespace redfish
{

/**
 * @brief Get service alerts enabled state
 *
 * @param[in] asyncResp     Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getServiceAlertsEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Get service alerts enabled state");

    dbus::utility::getProperty<bool>(
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/hardware_isolation/send_service_alerts",
        "xyz.openbmc_project.Object.Enable", "Enabled",
        [asyncResp](const boost::system::error_code& ec, bool enabled) {
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    BMCWEB_LOG_DEBUG(
                        "SendServiceAlerts D-Bus object does not exist");
                    return;
                }

                BMCWEB_LOG_ERROR("DBUS response error: {}", ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res.jsonValue["Oem"]["IBM"]["@odata.type"] =
                "#IBMComputerSystem.v1_0_0.IBM";
            asyncResp->res.jsonValue["Oem"]["IBM"]["SendServiceAlerts"] =
                enabled;
        });
}

/**
 * @brief Set service alerts enabled state
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 * @param[in] enabled     Service alerts enabled state from request.
 *
 * @return None.
 */
inline void setServiceAlertsEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, bool enabled)
{
    BMCWEB_LOG_DEBUG("Set service alerts enabled state to: {}", enabled);
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/hardware_isolation/send_service_alerts",
        "xyz.openbmc_project.Object.Enable", "Enabled", enabled,
        [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    BMCWEB_LOG_DEBUG(
                        "SendServiceAlerts D-Bus object does not exist");
                    return;
                }
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Service alerts setting updated successfully");
        });
}
} // namespace redfish
