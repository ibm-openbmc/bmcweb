#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "redfish_util.hpp"

#include <variant>

namespace redfish
{

static constexpr const char* usbCodeUpdateObjectPath =
    "/xyz/openbmc_project/control/service/_70hosphor_2dusb_2dcode_2dupdate";

/**
 * @brief Get the service name of the path
 *
 * @param[in] aResp     Shared pointer for generating response message.
 * @param[in] path      The D-Bus Object path
 * @param[in] handler   Call back
 *
 * @return None.
 */
template <typename Handler>
inline void getServiceName(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& path, Handler&& handler)
{
    // Map of service name to list of interfaces
    using MapperServiceMap =
        std::vector<std::pair<std::string, std::vector<std::string>>>;

    // Map of object paths to MapperServiceMaps
    using MapperGetSubTreeResponse =
        std::vector<std::pair<std::string, MapperServiceMap>>;

    crow::connections::systemBus->async_method_call(
        [aResp, path,
         handler{std::move(handler)}](const boost::system::error_code ec,
                                      const MapperGetSubTreeResponse& subtree) {
        std::string service{};
        if (ec)
        {
            BMCWEB_LOG_ERROR << "DBUS response error, ec: " << ec.value();
            messages::internalError(aResp->res);
            return;
        }

        for (const auto& [objectPath, serviceMap] : subtree)
        {
            if (objectPath != path)
            {
                continue;
            }

            if (serviceMap[0].first.empty() || serviceMap.size() != 1)
            {
                BMCWEB_LOG_ERROR << "usb code update mapper error!";
                messages::internalError(aResp->res);
                return;
            }

            handler(serviceMap[0].first);
            return;
        }

        BMCWEB_LOG_DEBUG << "Can't find usb code update service!";
        return;
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Control.Service.Attributes"});
}

/**
 * @brief Retrieves BMC USB code update state.
 *
 * @param[in] aResp     Shared pointer for generating response message.
 *
 * @return None.
 */
inline void
    getUSBCodeUpdateState(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get USB code update state";
    auto callback = [aResp](const std::string& service) {
        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus, service, usbCodeUpdateObjectPath,
            "xyz.openbmc_project.Control.Service.Attributes", "Enabled",
            [aResp](const boost::system::error_code& ec,
                    bool usbCodeUpdateState) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DBUS response error " << ec;
                messages::internalError(aResp->res);
                return;
            }

            aResp->res.jsonValue["Oem"]["IBM"]["@odata.type"] =
                "#OemManager.IBM";
            aResp->res.jsonValue["Oem"]["IBM"]["@odata.id"] =
                "/redfish/v1/Managers/bmc#/Oem/IBM";
            aResp->res.jsonValue["Oem"]["IBM"]["USBCodeUpdateEnabled"] =
                usbCodeUpdateState;
        });
    };
    getServiceName(aResp, usbCodeUpdateObjectPath, std::move(callback));
}

/**
 * @brief Sets BMC USB code update state.
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] state   USB code update state from request.
 *
 * @return None.
 */
inline void
    setUSBCodeUpdateState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const bool& state)
{
    BMCWEB_LOG_DEBUG << "Set USB code update status.";

    auto callback = [aResp, state](const std::string& service) {
        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "Can't set USB code update status. Error: "
                                 << ec;
                messages::internalError(aResp->res);
                return;
            }
        },
            service, usbCodeUpdateObjectPath, "org.freedesktop.DBus.Properties",
            "Set", "xyz.openbmc_project.Control.Service.Attributes", "Enabled",
            std::variant<bool>(state));
    };
    getServiceName(aResp, usbCodeUpdateObjectPath, std::move(callback));
}
} // namespace redfish
