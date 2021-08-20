#pragma once

#include "async_resp.hpp"

#include <app.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/container/flat_set.hpp>
#include <dbus_singleton.hpp>

#include <variant>

namespace redfish
{

struct ibmHealthPopulate : std::enable_shared_from_this<ibmHealthPopulate>
{
    ibmHealthPopulate(const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn) :
        asyncResp(asyncRespIn)
    {}

    ~ibmHealthPopulate()
    {
        asyncResp->res.jsonValue["Status"]["Health"] = "OK";
        asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
        auto& async = asyncResp;

        for (const auto& status : statuses)
        {
            const std::string& path = status.first;
            bool isSelf =
                selfPath ? boost::starts_with(path, *selfPath) : false;

            if (!isSelf)
            {
                for (const std::string& child : inventory)
                {
                    std::cout << "dzjtest 0000  child =" << child
                              << " path =" << path << "\n";
                    if (child == path)
                    {
                        const std::string& serviceName =
                            status.second.begin()->first;
                        crow::connections::systemBus->async_method_call(
                            [async](const boost::system::error_code ec,
                                    const std::variant<bool>& temphealth) {
                                if (ec)
                                {
                                    return;
                                }

                                const bool* value =
                                    std::get_if<bool>(&temphealth);
                                if (value == nullptr)
                                {
                                    return;
                                }
                                std::cout << "dzjtest 4444 continue \n";
                                if (*value == false)
                                {
                                    async->res.jsonValue["Status"]["Health"] =
                                        "Critical";
                                    async->res
                                        .jsonValue["Status"]["HealthRollup"] =
                                        "Critical";
                                }
                            },
                            serviceName, path,
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.State.Decorator."
                            "OperationalStatus",
                            "Functional");
                    }
                    else
                    {
                        std::cout << "dzjtest 1111 continue \n";
                        continue;
                    }
                }
            }
            else
            {
                if (path == *selfPath)
                {
                    const std::string& serviceName =
                        status.second.begin()->first;
                    crow::connections::systemBus->async_method_call(
                        [async](const boost::system::error_code ec,
                                const std::variant<bool>& temphealth) {
                            if (ec)
                            {
                                return;
                            }

                            const bool* value = std::get_if<bool>(&temphealth);
                            if (value == nullptr)
                            {
                                return;
                            }
                            if (*value == false)
                            {
                                async->res.jsonValue["Status"]["Health"] =
                                    "Critical";
                                async->res.jsonValue["Status"]["HealthRollup"] =
                                    "Critical";
                                return;
                            }
                        },
                        serviceName, path, "org.freedesktop.DBus.Properties",
                        "Get",
                        "xyz.openbmc_project.State.Decorator.OperationalStatus",
                        "Functional");
                }
                else
                {
                    continue;
                }
            }
        }
    }

    void populate()
    {
        if (populated)
        {
            return;
        }
        populated = true;
        getAllStatus();
    }

    void getAllStatus()
    {
        std::shared_ptr<ibmHealthPopulate> self = shared_from_this();
        crow::connections::systemBus->async_method_call(
            [self](
                const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>>&
                    subtree) {
                if (ec)
                {
                    return;
                }
                self->statuses = std::move(subtree);
            },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", 0,
            std::array<const char*, 1>{
                "xyz.openbmc_project.State.Decorator.OperationalStatus"});
    }

  public:
    std::shared_ptr<bmcweb::AsyncResp> asyncResp;

    // self is used if health is for an individual items status, as this is the
    // 'lowest most' item, the rollup will equal the health
    std::optional<std::string> selfPath;

    std::vector<std::string> inventory;
    std::vector<std::pair<
        std::string,
        std::vector<std::pair<std::string, std::vector<std::string>>>>>
        statuses;
    bool populated = false;
}; // namespace redfish
} // namespace redfish
