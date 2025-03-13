// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2018 Intel Corporation
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"

#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace redfish
{

inline std::string getRoleFromPrivileges(std::string_view priv)
{
    if (priv == "priv-admin")
    {
        return "Administrator";
    }
    if (priv == "priv-user")
    {
        return "ReadOnly";
    }
    if (priv == "priv-operator")
    {
        return "Operator";
    }
    if (priv == "priv-oemibmserviceagent")
    {
        return "OemIBMServiceAgent";
    }
    return "";
}

inline std::optional<nlohmann::json::array_t> getAssignedPrivFromRole(
    std::string_view role)
{
    nlohmann::json::array_t privArray;
    if ((role == "Administrator") || (role == "OemIBMServiceAgent"))
    {
        privArray.emplace_back("Login");
        privArray.emplace_back("ConfigureManager");
        privArray.emplace_back("ConfigureUsers");
        privArray.emplace_back("ConfigureSelf");
        privArray.emplace_back("ConfigureComponents");
    }
    else if (role == "Operator")
    {
        privArray.emplace_back("Login");
        privArray.emplace_back("ConfigureSelf");
        privArray.emplace_back("ConfigureComponents");
    }
    else if (role == "ReadOnly")
    {
        privArray.emplace_back("Login");
        privArray.emplace_back("ConfigureSelf");
    }
    else
    {
        return std::nullopt;
    }
    return privArray;
}

inline bool getOemPrivFromRole(std::string_view role, nlohmann::json& privArray)
{
    if ((role == "Administrator") || (role == "Operator") ||
        (role == "ReadOnly") || (role == "NoAccess"))
    {
        privArray = nlohmann::json::array();
    }
    else if (role == "OemIBMServiceAgent")
    {
        privArray = {"OemIBMPerformService"};
    }
    else
    {
        return false;
    }
    return true;
}

inline bool isRestrictedRole(const std::string& role)
{
    return ((role == "Operator") || (role == "OemIBMServiceAgent"));
}

inline void requestRoutesRoles(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/Roles/<str>/")
        .privileges(redfish::privileges::getRole)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& roleId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                std::optional<nlohmann::json::array_t> privArray =
                    getAssignedPrivFromRole(roleId);
                if (!privArray)
                {
                    messages::resourceNotFound(asyncResp->res, "Role", roleId);
                    return;
                }

                nlohmann::json oemPrivArray = nlohmann::json::array();
                if (!getOemPrivFromRole(roleId, oemPrivArray))
                {
                    messages::resourceNotFound(asyncResp->res, "Role", roleId);
                    return;
                }

                asyncResp->res.jsonValue["@odata.type"] = "#Role.v1_2_2.Role";
                asyncResp->res.jsonValue["Name"] = "User Role";
                asyncResp->res.jsonValue["OemPrivileges"] =
                    std::move(oemPrivArray);
                asyncResp->res.jsonValue["IsPredefined"] = true;
                asyncResp->res.jsonValue["Id"] = roleId;
                asyncResp->res.jsonValue["RoleId"] = roleId;
                asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
                    "/redfish/v1/AccountService/Roles/{}", roleId);
                asyncResp->res.jsonValue["AssignedPrivileges"] =
                    std::move(*privArray);
                asyncResp->res.jsonValue["Restricted"] =
                    isRestrictedRole(roleId);
                if (roleId == "OemIBMServiceAgent")
                {
                    asyncResp->res.jsonValue["Description"] = "ServiceAgent";
                }
                else
                {
                    asyncResp->res.jsonValue["Description"] =
                        roleId + " User Role";
                }
            });
}

inline void requestRoutesRoleCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/Roles/")
        .privileges(redfish::privileges::getRoleCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/AccountService/Roles";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#RoleCollection.RoleCollection";
                asyncResp->res.jsonValue["Name"] = "Roles Collection";
                asyncResp->res.jsonValue["Description"] = "BMC User Roles";

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.User.Manager",
                    "/xyz/openbmc_project/user",
                    "xyz.openbmc_project.User.Manager", "AllPrivileges",
                    [asyncResp](const boost::system::error_code& ec,
                                const std::vector<std::string>& privList) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        nlohmann::json& memberArray =
                            asyncResp->res.jsonValue["Members"];
                        memberArray = nlohmann::json::array();
                        for (const std::string& priv : privList)
                        {
                            std::string role = getRoleFromPrivileges(priv);
                            if (!role.empty())
                            {
                                nlohmann::json::object_t member;
                                member["@odata.id"] = boost::urls::format(
                                    "/redfish/v1/AccountService/Roles/{}",
                                    role);
                                memberArray.emplace_back(std::move(member));
                            }
                        }
                        asyncResp->res.jsonValue["Members@odata.count"] =
                            memberArray.size();
                    });
            });
}

} // namespace redfish
