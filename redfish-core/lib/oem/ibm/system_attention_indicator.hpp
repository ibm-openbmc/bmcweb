#pragma once

#include "async_resp.hpp"

#include <variant>

namespace redfish
{

/**
 * @brief Get the service name of the LED manager
 *
 * @param[in] aResp     Shared pointer for generating response message.
 * @param[in] path      The D-Bus Object path
 * @param[in] handler   Call back
 *
 * @return None.
 */
template <typename Handler>
inline void getService(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
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
                BMCWEB_LOG_DEBUG << "DBUS response error, ec: " << ec.value();
            }

            for (const auto& [objectPath, serviceMap] : subtree)
            {
                if (objectPath != path)
                {
                    continue;
                }

                for (const auto& [serviceName, interfaceList] : serviceMap)
                {
                    handler(serviceName, objectPath);
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project", 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Led.Group"});
}

/**
 * @brief Get System Attention Indicator
 *
 * @param[in] aResp             Shared pointer for generating response message.
 * @param[in] propertyValue     The property value
 *                              (PartitionSystemAttentionIndicator/PlatformSystemAttentionIndicator).
 *
 * @return None.
 */
inline void getSAI(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                   const std::string& propertyValue)
{
    BMCWEB_LOG_DEBUG << "Get platform/partition system attention indicator";

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
        messages::propertyUnknown(aResp->res, propertyValue);
        return;
    }

    auto callback = [aResp, propertyValue](const std::string& serviceName,
                                           const std::string& objectPath) {
        crow::connections::systemBus->async_method_call(
            [aResp, propertyValue](const boost::system::error_code ec,
                                   const std::variant<bool> asserted) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR << "async_method_call failed with ec "
                                     << ec.value();
                    messages::internalError(aResp->res);
                    return;
                }

                const bool* ledOn = std::get_if<bool>(&asserted);
                if (!ledOn)
                {
                    BMCWEB_LOG_ERROR << "Fail to get Asserted status ";
                    messages::internalError(aResp->res);
                    return;
                }

                nlohmann::json& oemSAI = aResp->res.jsonValue["Oem"]["IBM"];
                oemSAI["@odata.type"] = "#OemComputerSystem.IBM";
                if (propertyValue == "PartitionSystemAttentionIndicator")
                {
                    oemSAI["PartitionSystemAttentionIndicator"] = *ledOn;
                }
                else if (propertyValue == "PlatformSystemAttentionIndicator")
                {
                    oemSAI["PlatformSystemAttentionIndicator"] = *ledOn;
                }
            },
            serviceName, objectPath, "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Led.Group", "Asserted");
    };
    getService(aResp, "/xyz/openbmc_project/led/groups/" + name,
               std::move(callback));
}

/**
 * @brief Set System Attention Indicator
 *
 * @param[in] aResp             Shared pointer for generating response message.
 * @param[in] propertyValue     The property value
 *                              (PartitionSystemAttentionIndicator/PlatformSystemAttentionIndicator).
 * @param[in] value             true or fasle
 *
 * @return None.
 */
inline void setSAI(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                   const std::string& propertyValue, bool value)
{
    BMCWEB_LOG_DEBUG << "Set platform/partition system attention indicator";

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

    auto callback = [aResp, value](const std::string& serviceName,
                                   const std::string& objectPath) {
        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR << "async_method_call failed with ec "
                                     << ec.value();
                    messages::internalError(aResp->res);
                    return;
                }
            },
            serviceName, objectPath, "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.Led.Group", "Asserted",
            std::variant<bool>(value));
    };
    getService(aResp, "/xyz/openbmc_project/led/groups/" + name,
               std::move(callback));
}
} // namespace redfish
