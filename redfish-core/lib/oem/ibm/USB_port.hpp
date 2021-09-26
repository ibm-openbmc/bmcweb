#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "redfish_util.hpp"

#include <variant>

namespace redfish
{

template <typename Handler>
inline void getServiceName(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           Handler&& handler)
{
    crow::connections::systemBus->async_method_call(
        [aResp, handler{std::move(handler)}](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                getObjectType) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "ObjectMapper::GetObject call failed: "
                                 << ec;
                messages::internalError(aResp->res);
                return;
            }

            if (getObjectType.size() != 1)
            {
                BMCWEB_LOG_DEBUG << "Can't find bmc D-Bus object!";
                messages::internalError(aResp->res);
                return;
            }

            if (getObjectType[0].first.empty())
            {
                BMCWEB_LOG_DEBUG << "Error getting bmc D-Bus object!";
                messages::internalError(aResp->res);
                return;
            }

            const std::string service = getObjectType[0].first;
            BMCWEB_LOG_DEBUG << "GetObjectType: " << service;

            handler(service);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        "/xyz/openbmc_project/state/bmc0",
        std::array<const char*, 1>{"xyz.openbmc_project.State.BMC"});
}

/**
 * @brief Retrieves BMC USB ports state.
 *
 * @param[in] aResp     Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getUSBPortState(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get USB port state";
    auto callback = [aResp](const std::string& service) {
        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec,
                    std::variant<std::string>& usbState) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
                    messages::internalError(aResp->res);
                    return;
                }

                const std::string* usbStatePtr =
                    std::get_if<std::string>(&usbState);

                if (!usbStatePtr)
                {
                    BMCWEB_LOG_DEBUG << "Can't get USB port status!";
                    messages::internalError(aResp->res);
                    return;
                }

                if (*usbStatePtr ==
                    "xyz.openbmc_project.State.BMC.USBState.UsbDisabled")
                {
                    aResp->res.jsonValue["Oem"]["IBM"]["USBPortState"] =
                        "Disabled";
                }
                else if (*usbStatePtr == "xyz.openbmc_project.State.BMC."
                                         "USBState.UsbEnabled")
                {
                    aResp->res.jsonValue["Oem"]["IBM"]["USBPortState"] =
                        "Enabled";
                }
                else
                {
                    aResp->res.jsonValue["Oem"]["IBM"]["USBPortState"] = "";
                }
            },
            service, "/xyz/openbmc_project/state/bmc0",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.State.BMC", "CurrentUSBState");
    };
    getServiceName(aResp, std::move(callback));
}

/**
 * @brief Sets BMC USB ports state.
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] state   USB ports state from request.
 *
 * @return None.
 */
inline void setUSBPortState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            std::string state)
{
    BMCWEB_LOG_DEBUG << "Set USB port status.";

    if (state == "Disabled")
    {
        state = "xyz.openbmc_project.State.BMC.Transition.DisableUsb";
    }
    else if (state == "Enabled")
    {
        state = "xyz.openbmc_project.State.BMC.Transition.EnableUsb";
    }
    else
    {
        messages::propertyValueNotInList(aResp->res, state, "USBPortState");
        return;
    }

    auto callback = [aResp, state](const std::string& service) {
        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG << "Can't set USB port status. Error: "
                                     << ec;
                    messages::internalError(aResp->res);
                    return;
                }
            },
            service, "/xyz/openbmc_project/state/bmc0",
            "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.State.BMC", "RequestedBMCTransition",
            std::variant<std::string>(state));
    };
    getServiceName(aResp, std::move(callback));
}
} // namespace redfish
