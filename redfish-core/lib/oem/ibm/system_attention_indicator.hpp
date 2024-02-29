#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "led.hpp"
#include "logging.hpp"

#include <asm-generic/errno.h>

#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/property.hpp>

#include <memory>
#include <string>

namespace redfish
{
/**
 * @brief Get System Attention Indicator
 *
 * @param[in] asyncResp         Shared pointer for generating response message.
 * @param[in] propertyValue     The property value
 *                              (PartitionSystemAttentionIndicator/PlatformSystemAttentionIndicator).
 *
 * @return None.
 */
inline void getSAI(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& propertyValue)
{
    BMCWEB_LOG_DEBUG("Get platform/partition system attention indicator");

    std::string name{};
    if (propertyValue == "PartitionSystemAttentionIndicator")
    {
        name = "partition_system_attention_indicator";
    }
    else if (propertyValue == "PlatformSystemAttentionIndicator")
    {
        name = "platform_system_attention_indicator";
    }
    else
    {
        messages::propertyUnknown(asyncResp->res, propertyValue);
        return;
    }

    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/led/groups/" + name, ledGroupInterface,
        [asyncResp, name,
         propertyValue](const boost::system::error_code& ec,
                        const dbus::utility::MapperGetObject& object) {
            if (ec || object.empty())
            {
                BMCWEB_LOG_DEBUG("Failed to get LED DBus name: {}",
                                 ec.message());
                return;
            }

            dbus::utility::getProperty<bool>(
                object.begin()->first,
                "/xyz/openbmc_project/led/groups/" + name,
                "xyz.openbmc_project.Led.Group", "Asserted",
                [asyncResp, propertyValue](const boost::system::error_code& ec1,
                                           bool assert) {
                    if (ec1)
                    {
                        if (ec1.value() != EBADR)
                        {
                            BMCWEB_LOG_ERROR("DBUS response error: {}",
                                             ec1.message());
                            messages::internalError(asyncResp->res);
                        }
                        return;
                    }

                    nlohmann::json& oemSAI =
                        asyncResp->res.jsonValue["Oem"]["IBM"];
                    oemSAI["@odata.type"] = "#IBMComputerSystem.v1_0_0.IBM";
                    if (propertyValue == "PartitionSystemAttentionIndicator")
                    {
                        oemSAI["PartitionSystemAttentionIndicator"] = assert;
                    }
                    else if (propertyValue ==
                             "PlatformSystemAttentionIndicator")
                    {
                        oemSAI["PlatformSystemAttentionIndicator"] = assert;
                    }
                });
        });
}

/**
 * @brief Set System Attention Indicator
 *
 * @param[in] asyncResp         Shared pointer for generating response message.
 * @param[in] propertyValue     The property value
 *                              (PartitionSystemAttentionIndicator/PlatformSystemAttentionIndicator).
 * @param[in] value             true or false
 *
 * @return None.
 */
inline void setSAI(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& propertyValue, bool value)
{
    BMCWEB_LOG_DEBUG("Set platform/partition system attention indicator");

    std::string name{};
    if (propertyValue == "PartitionSystemAttentionIndicator")
    {
        name = "partition_system_attention_indicator";
    }
    else if (propertyValue == "PlatformSystemAttentionIndicator")
    {
        name = "platform_system_attention_indicator";
    }
    else
    {
        return;
    }

    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/led/groups/" + name, ledGroupInterface,
        [asyncResp, name, value](const boost::system::error_code& ec,
                                 const dbus::utility::MapperGetObject& object) {
            if (ec || object.empty())
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            sdbusplus::asio::setProperty(
                *crow::connections::systemBus, object.begin()->first,
                "/xyz/openbmc_project/led/groups/" + name,
                "xyz.openbmc_project.Led.Group", "Asserted", value,
                [asyncResp](const boost::system::error_code& ec1) {
                    if (ec1)
                    {
                        if (ec1.value() != EBADR)
                        {
                            BMCWEB_LOG_ERROR("DBUS response error: {}",
                                             ec1.message());
                            messages::internalError(asyncResp->res);
                        }
                        return;
                    }
                });
        });
}
} // namespace redfish
