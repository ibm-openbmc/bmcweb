
#pragma once
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"

#include <async_resp.hpp>
#include <boost/system/error_code.hpp>

#include <memory>
#include <string>

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
 * @param[i]   services   Service to exporting the D-bus object path
 * @param[i]   namePath   Json pointer to the name field to update.
 *
 * @return void
 */
inline void getPrettyName(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& path,
                          const std::string& serviceName,
                          const nlohmann::json::json_pointer& namePath)
{
    BMCWEB_LOG_DEBUG("Get PrettyName for: {}", path);

    dbus::utility::getProperty<std::string>(
        serviceName, path, "xyz.openbmc_project.Inventory.Item", "PrettyName",
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

/**
 * @brief Populate the collection "Members" from a GetSubTreePaths search of
 *        inventory
 *
 * @param[i,o] asyncResp  Async response object
 * @param[i]   path       D-bus object path to find the pretty name
 * @param[i]   services   List of services to exporting the D-bus object path
 * @param[i]   namePath   Json pointer to the name field to update.
 *
 * @return void
 */
inline void getPrettyName(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& path,
                          const dbus::utility::MapperServiceMap& services,
                          const nlohmann::json::json_pointer& namePath)
{
    BMCWEB_LOG_DEBUG("Get PrettyName with MapperServiceMap for: {}", path);

    // Ensure we only got one service back
    if (services.size() != 1)
    {
        BMCWEB_LOG_ERROR("Invalid Service Size {}", services.size());
        for (const auto& service : services)
        {
            BMCWEB_LOG_ERROR("Invalid Service Name: {}", service.first);
        }
        if (asyncResp)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }

    getPrettyName(asyncResp, path, services[0].first, namePath);
}

} // namespace name_util
} // namespace redfish
