#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "utils/dbus_utils.hpp"

#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string_view>

namespace redfish
{

static constexpr const char* usbCodeUpdateObjectPath =
    "/xyz/openbmc_project/control/service/_70hosphor_2dusb_2dcode_2dupdate";
static constexpr const char* usbCodeUpdateInterface =
    "xyz.openbmc_project.Control.Service.Attributes";

constexpr std::array<std::string_view, 1> usbCodeUpdateInterfaces = {
    usbCodeUpdateInterface};

inline void afterGetUSBCodeUpdateEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, bool usbCodeUpdateState)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["Oem"]["IBM"]["@odata.type"] =
        "#IBMManager.v1_0_0.IBM";
    asyncResp->res.jsonValue["Oem"]["IBM"]["@odata.id"] =
        "/redfish/v1/Managers/bmc#/Oem/IBM";
    asyncResp->res.jsonValue["Oem"]["IBM"]["USBCodeUpdateEnabled"] =
        usbCodeUpdateState;
}

inline void afterGetUSBCodeUpdateObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if ((ec == boost::system::errc::io_error) || object.empty())
    {
        BMCWEB_LOG_DEBUG("USB code update not found");
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    dbus::utility::getProperty<bool>(
        *crow::connections::systemBus, object.begin()->first,
        usbCodeUpdateObjectPath, usbCodeUpdateInterface, "Enabled",
        std::bind_front(afterGetUSBCodeUpdateEnabled, asyncResp));
}

/**
 * @brief Retrieves BMC USB code update state.
 *
 * @param[in] asyncResp     Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getUSBCodeUpdateState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Get USB code update state");
    dbus::utility::getDbusObject(
        usbCodeUpdateObjectPath, usbCodeUpdateInterfaces,
        std::bind_front(afterGetUSBCodeUpdateObject, asyncResp));
}

inline void afterSetUSBCodeUpdateState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const bool state,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    setDbusProperty(asyncResp, "Oem/IBM/USBCodeUpdateEnabled",
                    object.begin()->first,
                    sdbusplus::object_path(usbCodeUpdateObjectPath),
                    usbCodeUpdateInterface, "Enabled", state);
}

/**
 * @brief Sets BMC USB code update state.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 * @param[in] state       USB code update state from request.
 *
 * @return None.
 */
inline void setUSBCodeUpdateState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const bool& state)
{
    BMCWEB_LOG_DEBUG("Set USB code update status.");
    dbus::utility::getDbusObject(
        usbCodeUpdateObjectPath, usbCodeUpdateInterfaces,
        std::bind_front(afterSetUSBCodeUpdateState, asyncResp, state));
}
} // namespace redfish
