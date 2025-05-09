// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2019 Intel Corporation
#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "generated/enums/resource.hpp"
#include "logging.hpp"

#include <nlohmann/json.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <variant>
#include <vector>

namespace redfish
{

struct HealthPopulate : std::enable_shared_from_this<HealthPopulate>
{
    // By default populate status to "/Status" of |asyncResp->res.jsonValue|.
    explicit HealthPopulate(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn) :
        asyncResp(asyncRespIn), statusPtr("/Status")
    {}

    // Takes a JSON pointer rather than a reference. This is pretty useful when
    // the address of the status JSON might change, for example, elements in an
    // array.
    HealthPopulate(const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn,
                   const nlohmann::json::json_pointer& ptr) :
        asyncResp(asyncRespIn), statusPtr(ptr)
    {}

    HealthPopulate(const HealthPopulate&) = delete;
    HealthPopulate(HealthPopulate&&) = delete;
    HealthPopulate& operator=(const HealthPopulate&) = delete;
    HealthPopulate& operator=(const HealthPopulate&&) = delete;

    ~HealthPopulate()
    {
        nlohmann::json& jsonStatus = asyncResp->res.jsonValue[statusPtr];
        nlohmann::json& health = jsonStatus["Health"];
        nlohmann::json& rollup = jsonStatus["HealthRollup"];

        health = resource::Health::OK;
        rollup = resource::Health::OK;

        for (const std::shared_ptr<HealthPopulate>& healthChild : children)
        {
            healthChild->globalInventoryPath = globalInventoryPath;
            healthChild->statuses = statuses;
        }

        for (const auto& [path, interfaces] : statuses)
        {
            bool isSelf = false;
            if (selfPath)
            {
                if (path.str == *selfPath ||
                    path.str.starts_with(*selfPath + "/"))
                {
                    isSelf = true;
                }
            }

            // managers inventory is all the inventory, don't skip any
            if (!isManagersHealth && !isSelf)
            {
                // We only want to look at this association if either the path
                // of this association is an inventory item, or one of the
                // endpoints in this association is a child

                bool isChild = false;
                for (const std::string& child : inventory)
                {
                    if (path.str.starts_with(child))
                    {
                        isChild = true;
                        break;
                    }
                }
                if (!isChild)
                {
                    for (const auto& [interface, association] : interfaces)
                    {
                        if (interface != "xyz.openbmc_project.Association")
                        {
                            continue;
                        }
                        for (const auto& [name, value] : association)
                        {
                            if (name != "endpoints")
                            {
                                continue;
                            }

                            const std::vector<std::string>* endpoints =
                                std::get_if<dbus::utility::MapperEndPoints>(
                                    &value);
                            if (endpoints == nullptr)
                            {
                                BMCWEB_LOG_ERROR("Illegal association at {}",
                                                 path.str);
                                continue;
                            }
                            bool containsChild = false;
                            for (const std::string& endpoint : *endpoints)
                            {
                                if (std::ranges::find(inventory, endpoint) !=
                                    inventory.end())
                                {
                                    containsChild = true;
                                    break;
                                }
                            }
                            if (!containsChild)
                            {
                                continue;
                            }
                        }
                    }
                }
            }

            if (path.str.starts_with(globalInventoryPath) &&
                path.str.ends_with("critical"))
            {
                rollup = resource::Health::Critical;
                return;
            }
            if (path.str.starts_with(globalInventoryPath) &&
                path.str.ends_with("warning"))
            {
                health = resource::Health::Warning;
                if (rollup != resource::Health::Critical)
                {
                    rollup = resource::Health::Warning;
                }
            }
            else if (path.str.ends_with("critical"))
            {
                rollup = resource::Health::Critical;
                if (isSelf)
                {
                    health = resource::Health::Critical;
                    return;
                }
            }
            else if (path.str.ends_with("warning"))
            {
                if (rollup != resource::Health::Critical)
                {
                    rollup = resource::Health::Warning;
                }

                if (isSelf)
                {
                    health = resource::Health::Warning;
                }
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
        getAllStatusAssociations();
        getGlobalPath();
    }

    void getGlobalPath()
    {
        constexpr std::array<std::string_view, 1> interfaces = {
            "xyz.openbmc_project.Inventory.Item.Global"};
        std::shared_ptr<HealthPopulate> self = shared_from_this();
        dbus::utility::getSubTreePaths(
            "/", 0, interfaces,
            [self](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetSubTreePathsResponse& resp) {
                if (ec || resp.size() != 1)
                {
                    // no global item, or too many
                    return;
                }
                self->globalInventoryPath = resp[0];
            });
    }

    void getAllStatusAssociations()
    {
        std::shared_ptr<HealthPopulate> self = shared_from_this();
        sdbusplus::message::object_path path("/");
        dbus::utility::getManagedObjects(
            "xyz.openbmc_project.ObjectMapper", path,
            [self](const boost::system::error_code& ec,
                   const dbus::utility::ManagedObjectType& resp) {
                if (ec)
                {
                    return;
                }
                self->statuses = resp;
                for (auto it = self->statuses.begin();
                     it != self->statuses.end();)
                {
                    if (it->first.str.ends_with("critical") ||
                        it->first.str.ends_with("warning"))
                    {
                        it++;
                        continue;
                    }
                    it = self->statuses.erase(it);
                }
            });
    }

    std::shared_ptr<bmcweb::AsyncResp> asyncResp;

    // Will populate the health status into |asyncResp_json[statusPtr]|
    nlohmann::json::json_pointer statusPtr;

    // we store pointers to other HealthPopulate items so we can update their
    // members and reduce dbus calls. As we hold a shared_ptr to them, they get
    // destroyed last, and they need not call populate()
    std::vector<std::shared_ptr<HealthPopulate>> children;

    // self is used if health is for an individual items status, as this is the
    // 'lowest most' item, the rollup will equal the health
    std::optional<std::string> selfPath;

    std::vector<std::string> inventory;
    bool isManagersHealth = false;
    dbus::utility::ManagedObjectType statuses;
    std::string globalInventoryPath = "-"; // default to illegal dbus path
    bool populated = false;
};
} // namespace redfish
