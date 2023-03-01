
#pragma once
#include <async_resp.hpp>

#include <algorithm>
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
 * @param[i]   path       D-bus object path to find the find pretty name
 * @param[i]   services   List of services to exporting the D-bus object path
 * @param[i]   namePath   Json pointer to the name field to update.
 *
 * @return void
 */
inline void getPrettyName(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        services,
    const nlohmann::json::json_pointer& namePath)
{
    BMCWEB_LOG_DEBUG << "Get PrettyName for: " << path;

    // Ensure we only got one service back
    if (services.size() != 1)
    {
        BMCWEB_LOG_ERROR << "Invalid Service Size " << services.size();
        if (asyncResp)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, services[0].first, path,
        "xyz.openbmc_project.Inventory.Item", "PrettyName",

        [asyncResp, path, namePath](const boost::system::error_code ec,
                                    const std::string& prettyName) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
            return;
        }

        if (prettyName.empty())
        {
            return;
        }

        BMCWEB_LOG_DEBUG << "Pretty Name: " << prettyName;

        asyncResp->res.jsonValue[namePath] = prettyName;
        });
}

} // namespace name_util
} // namespace redfish
