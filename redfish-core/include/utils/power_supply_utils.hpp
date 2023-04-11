#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"

#include <boost/system/error_code.hpp>
#include <sdbusplus/message.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace redfish
{

namespace power_supply_utils
{

inline void getInputHistoryPaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& validPowerSupplyPath,
    std::function<void(const std::vector<std::string>& historyPaths)>&&
        callback)
{
    std::string associationPath = validPowerSupplyPath + "/input_history";
    dbus::utility::getAssociationEndPoints(
        associationPath, [asyncResp, callback{std::move(callback)}](
                             const boost::system::error_code& ec,
                             const dbus::utility::MapperEndPoints& endpoints) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR << "D-Bus response error: " << ec;
                    messages::internalError(asyncResp->res);
                    return;
                }

                // Association does not exist.  This is a valid situation; some
                // power supplies do not have input power history.  Pass an
                // empty vector to the callback.
                callback(std::vector<std::string>{});
                return;
            }

            callback(endpoints);
        });
}

inline bool checkPowerSupplyId(const std::string& powerSupplyPath,
                               const std::string& powerSupplyId)
{
    std::string powerSupplyName =
        sdbusplus::message::object_path(powerSupplyPath).filename();

    return !(powerSupplyName.empty() || powerSupplyName != powerSupplyId);
}

inline void getValidPowerSupplyPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& validChassisPath, const std::string& powerSupplyId,
    std::function<void(const std::string& powerSupplyPath)>&& callback)
{
    std::string powerPath = validChassisPath + "/powered_by";
    dbus::utility::getAssociationEndPoints(
        powerPath, [asyncResp, powerSupplyId, callback{std::move(callback)}](
                       const boost::system::error_code& ec,
                       const dbus::utility::MapperEndPoints& endpoints) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR << "D-Bus response error: " << ec;
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            for (const auto& endpoint : endpoints)
            {
                if (checkPowerSupplyId(endpoint, powerSupplyId))
                {
                    callback(endpoint);
                    return;
                }
            }

            if (!endpoints.empty())
            {
                messages::resourceNotFound(asyncResp->res, "PowerSupplies",
                                           powerSupplyId);
                return;
            }
        });
}

} // namespace power_supply_utils
} // namespace redfish
