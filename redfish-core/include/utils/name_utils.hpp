
#pragma once
#include "dbus_utility.hpp"

#include <async_resp.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace redfish
{
namespace name_util
{

/**
 * @brief Populate the collection "Members" from a GetSubTreePaths search of
 *        inventory
 *
 * @param[i,o] asyncResp  Async response object
 * @param[i]   path       D-bus object path to find the pretty name
 * @param[i]   service    Service for Inventory interface
 * @param[i]   namePath   Json pointer to the name field to update.
 *
 * @return void
 */
inline void getPrettyName(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& path, const std::string& service,
                          const nlohmann::json::json_pointer& namePath)
{
    BMCWEB_LOG_DEBUG("Get PrettyName for: {}", path);

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Inventory.Item", "PrettyName",

        [asyncResp, path, namePath](const boost::system::error_code& ec,
                                    const std::string& prettyName) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error : {}", ec.value());
            return;
        }

        if (prettyName.empty())
        {
            return;
        }

        BMCWEB_LOG_DEBUG("Pretty Name: {}", prettyName);

        asyncResp->res.jsonValue[namePath] = prettyName;
    });
}

} // namespace name_util
} // namespace redfish
