#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "redfish_util.hpp"

#include <variant>

namespace redfish
{

/**
 * @brief Retrieves lamp test state.
 *
 * @param[in] aResp     Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getLampTestState(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get lamp test state";

    std::array<const char*, 1> interfaces = {"xyz.openbmc_project.Led.Group"};
    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/led/groups/lamp_test", interfaces,
        [aResp](const boost::system::error_code& ec,
                const dbus::utility::MapperGetObject& object) {
        if (ec || object.empty())
        {
            BMCWEB_LOG_ERROR << "DBUS response error " << ec.message();
            messages::internalError(aResp->res);
            return;
        }

        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus, object.begin()->first,
            "/xyz/openbmc_project/led/groups/lamp_test",
            "xyz.openbmc_project.Led.Group", "Asserted",
            [aResp](const boost::system::error_code& ec1, bool assert) {
            if (ec1)
            {
                if (ec1.value() != EBADR)
                {
                    messages::internalError(aResp->res);
                }
                return;
            }

            aResp->res.jsonValue["Oem"]["@odata.type"] =
                "#OemComputerSystem.Oem";
            aResp->res.jsonValue["Oem"]["IBM"]["@odata.type"] =
                "#OemComputerSystem.IBM";
            aResp->res.jsonValue["Oem"]["IBM"]["LampTest"] = assert;
            });
        });
}

/**
 * @brief Sets lamp test state.
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] state   Lamp test state from request.
 *
 * @return None.
 */
inline void setLampTestState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const bool state)
{
    BMCWEB_LOG_DEBUG << "Set lamp test status.";

    std::array<const char*, 1> interfaces = {"xyz.openbmc_project.Led.Group"};
    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/led/groups/lamp_test", interfaces,
        [aResp, state](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetObject& object) {
        if (ec || object.empty())
        {
            BMCWEB_LOG_ERROR << "DBUS response error " << ec.message();
            messages::internalError(aResp->res);
            return;
        }

        sdbusplus::asio::setProperty(
            *crow::connections::systemBus, object.begin()->first,
            "/xyz/openbmc_project/led/groups/lamp_test",
            "xyz.openbmc_project.Led.Group", "Asserted", state,
            [aResp](const boost::system::error_code& ec1) {
            if (ec1)
            {
                if (ec1.value() != EBADR)
                {
                    messages::internalError(aResp->res);
                }
                return;
            }
            });
        });
}

} // namespace redfish
