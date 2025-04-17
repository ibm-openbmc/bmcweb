#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"

#include <asm-generic/errno.h>

#include <boost/system/error_code.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace redfish
{

namespace systems_utils
{
inline void afterGetValidSystemsPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemId,
    const std::function<void(const std::optional<std::string>&)>& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& systemsPaths)
{
    BMCWEB_LOG_DEBUG("getValidSystemsPath respHandler enter");
    if (ec)
    {
        if (ec.value() == EBADR)
        {
            BMCWEB_LOG_DEBUG("No systems found");
            callback("");
            return;
        }
        BMCWEB_LOG_ERROR("getValidSystemsPath respHandler DBUS error: {}",
                         ec.value());
        messages::internalError(asyncResp->res);
        return;
    }

    for (const std::string& system : systemsPaths)
    {
        sdbusplus::message::object_path path(system);
        if (path.filename() == systemId)
        {
            callback(path);
            return;
        }
    }
    BMCWEB_LOG_DEBUG("No system named {} found", systemId);
    callback("");
}

inline void getValidSystemsPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemId,
    std::function<void(const std::optional<std::string>&)>&& callback)
{
    BMCWEB_LOG_DEBUG("checkSystemsId enter");

    // Get the Systems Collection
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.System"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, systemId, callback{std::move(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& systemsPaths) {
            afterGetValidSystemsPath(asyncResp, systemId, callback, ec,
                                     systemsPaths);
            return;
        });
}

} // namespace systems_utils
} // namespace redfish
