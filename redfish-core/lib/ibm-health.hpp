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
        asyncResp(asyncRespIn), jsonStatus(asyncResp->res.jsonValue["Status"])
    {}

    ibmHealthPopulate(const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn,
                      nlohmann::json& status) :
        asyncResp(asyncRespIn),
        jsonStatus(status)
    {}

    ~ibmHealthPopulate()
    {
        nlohmann::json& health = jsonStatus["Health"];
        nlohmann::json& rollup = jsonStatus["HealthRollup"];

        health = "OK";
        rollup = "OK";

        for (const auto& status : statuses)
        {
            bool isSelf =
                selfPath ? boost::starts_with(path.str, *selfPath) : false;

            if (!isSelf)
            {
                std::vector<std::string> values;
                for (const std::string& child : inventory)
                {
                    auto serviceNames = status.find(child);
                    const std::string& path = serviceNames[0];
                    const std::string& serviceName = serviceNames[1].first;
                    crow::connections::systemBus->async_method_call(
                        [asyncResp, &values](const boost::system::error_code ec,
                                             const std::variant<bool>& health) {
                            if (ec)
                            {
                                BMCWEB_LOG_DEBUG << "Can't get Fan health!";
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            const bool* value = std::get_if<bool>(&health);
                            const std::string& verifyString = "verify";
                            if (value == nullptr)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            if (*value == false)
                            {
                                values.push_back(verifyString);
                            }
                            else
                            {
                                continue;
                            }
                        },
                        serviceName, path, "org.freedesktop.DBus.Properties",
                        "Get",
                        "xyz.openbmc_project.State.Decorator.OperationalStatus",
                        "Functional");
                }
                if (!values.empty)
                {
                    health = "Critical";
                    rollup = "Critical";
                    return;
                }
            }
            else
            {
                auto serviceNames = status.find(child);
                const std::string& path = serviceNames[0];
                const std::string& serviceName = serviceNames[1].first;
                crow::connections::systemBus->async_method_call(
                    [asyncResp](const boost::system::error_code ec,
                                const std::variant<bool>& health) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG << "Can't get Fan health!";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        const bool* value = std::get_if<bool>(&health);
                        if (value == nullptr)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (*value == false)
                        {
                            health = "Critical";
                            rollup = "Critical";
                            return;
                        }
                    },
                    serviceName, path, "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.State.Decorator.OperationalStatus",
                    "Functional");
            }
        }
    }

    // this should only be called once per url, others should get updated by
    // being added as children to the 'main' health object for the page
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
            [self](const boost::system::error_code ec,
                   const std::pair<std::string,
                                   std::vector<std::pair<
                                       std::string, std::vector<std::string>>>>&
                       resp) {
                if (ec)
                {
                    return;
                }
                self->statuses = std::move(resp);
            },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", 0,
            std::array<
                const char*,
                1> "xyz.openbmc_project.State.Decorator.OperationalStatus");
    }

    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    nlohmann::json& jsonStatus;

    // self is used if health is for an individual items status, as this is the
    // 'lowest most' item, the rollup will equal the health
    std::optional<std::string> selfPath;

    std::vector<std::string> inventory;
    const std::pair<
        std::string,
        std::vector<std::pair<std::string, std::vector<std::string>>>>
        statuses;
    bool populated = false;
};
} // namespace redfish
