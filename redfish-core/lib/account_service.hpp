/*
// Copyright (c) 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once

#include <app.hpp>
#include <dbus_utility.hpp>
#include <error_messages.hpp>
#include <openbmc_dbus_rest.hpp>
#include <persistent_data.hpp>
#include <registries/privilege_registry.hpp>
#include <roles.hpp>
#include <utils/json_utils.hpp>

#include <string>
#include <variant>
#include <vector>

namespace redfish
{

constexpr const char* ldapConfigObjectName =
    "/xyz/openbmc_project/user/ldap/openldap";
constexpr const char* adConfigObject =
    "/xyz/openbmc_project/user/ldap/active_directory";

constexpr const char* ldapRootObject = "/xyz/openbmc_project/user/ldap";
constexpr const char* ldapDbusService = "xyz.openbmc_project.Ldap.Config";
constexpr const char* ldapConfigInterface =
    "xyz.openbmc_project.User.Ldap.Config";
constexpr const char* ldapCreateInterface =
    "xyz.openbmc_project.User.Ldap.Create";
constexpr const char* ldapEnableInterface = "xyz.openbmc_project.Object.Enable";
constexpr const char* ldapPrivMapperInterface =
    "xyz.openbmc_project.User.PrivilegeMapper";
constexpr const char* dbusObjManagerIntf = "org.freedesktop.DBus.ObjectManager";
constexpr const char* propertyInterface = "org.freedesktop.DBus.Properties";
constexpr const char* mapperBusName = "xyz.openbmc_project.ObjectMapper";
constexpr const char* mapperObjectPath = "/xyz/openbmc_project/object_mapper";
constexpr const char* mapperIntf = "xyz.openbmc_project.ObjectMapper";

struct LDAPRoleMapData
{
    std::string groupName;
    std::string privilege;
};

struct LDAPConfigData
{
    std::string uri{};
    std::string bindDN{};
    std::string baseDN{};
    std::string searchScope{};
    std::string serverType{};
    bool serviceEnabled = false;
    std::string userNameAttribute{};
    std::string groupAttribute{};
    std::vector<std::pair<std::string, LDAPRoleMapData>> groupRoleList;
};

using DbusVariantType =
    std::variant<bool, int32_t, std::string, std::vector<std::string>>;

using DbusInterfaceType = boost::container::flat_map<
    std::string, boost::container::flat_map<std::string, DbusVariantType>>;

using ManagedObjectType =
    std::vector<std::pair<sdbusplus::message::object_path, DbusInterfaceType>>;

using GetObjectType = dbus::utility::MapperGetObject;

inline std::string getRoleIdFromPrivilege(std::string_view role)
{
    if (role == "priv-admin")
    {
        return "Administrator";
    }
    if (role == "priv-user")
    {
        return "ReadOnly";
    }
    if (role == "priv-operator")
    {
        return "Operator";
    }
    if (role == "priv-oemibmserviceagent")
    {
        return "OemIBMServiceAgent";
    }
    return "";
}

inline std::string getPrivilegeFromRoleId(std::string_view role)
{
    if (role == "Administrator")
    {
        return "priv-admin";
    }
    if (role == "ReadOnly")
    {
        return "priv-user";
    }
    if (role == "Operator")
    {
        return "priv-operator";
    }
    if (role == "OemIBMServiceAgent")
    {
        return "priv-oemibmserviceagent";
    }
    return "";
}

inline bool getAccountTypeFromUserGroup(std::string_view userGroup,
                                        nlohmann::json& accountTypes)
{
    // set false if userGroup values are not found in list, return error
    bool isFoundUserGroup = true;

    if (userGroup == "redfish")
    {
        accountTypes.push_back("Redfish");
    }
    else if (userGroup == "ipmi")
    {
        accountTypes.push_back("IPMI");
    }
    else if (userGroup == "ssh")
    {
        accountTypes.push_back("HostConsole");
        accountTypes.push_back("ManagerConsole");
    }
    else if (userGroup == "web")
    {
        accountTypes.push_back("WebUI");
    }
    else
    {
        // set false if userGroup not found
        isFoundUserGroup = false;
    }

    return isFoundUserGroup;
}

inline bool getUserGroupFromAccountType(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::optional<std::vector<std::string>>& accountTypes,
    std::vector<std::string>& userGroup)
{
    // set false if AccountTypes values are not found in list, return error
    bool isFoundAccountTypes = true;

    bool isRedfish = false;
    bool isIPMI = false;
    bool isHostConsole = false;
    bool isManagerConsole = false;
    bool isWebUI = false;

    for (const auto& accountType : *accountTypes)
    {
        if (accountType == "Redfish")
        {
            isRedfish = true;
        }
        else if (accountType == "IPMI")
        {
            isIPMI = true;
        }
        else if (accountType == "WebUI")
        {
            isWebUI = true;
        }
        else if ((accountType == "HostConsole"))
        {
            isHostConsole = true;
        }
        else if (accountType == "ManagerConsole")
        {
            isManagerConsole = true;
        }
        else
        {
            // set false if accountTypes not found and return
            isFoundAccountTypes = false;
            messages::propertyValueNotInList(asyncResp->res, "AccountTypes",
                                             accountType);
            return isFoundAccountTypes;
        }
    }

    if ((isHostConsole) ^ (isManagerConsole))
    {
        BMCWEB_LOG_ERROR << "HostConsole or ManagerConsole, one of value is "
                            "missing to set SSH property";
        isFoundAccountTypes = false;
        messages::strictAccountTypes(asyncResp->res, "AccountTypes");
        return isFoundAccountTypes;
    }
    if (isRedfish)
    {
        userGroup.emplace_back("redfish");
    }
    if (isIPMI)
    {
        userGroup.emplace_back("ipmi");
    }
    if (isWebUI)
    {
        userGroup.emplace_back("web");
    }

    if ((isHostConsole) && (isManagerConsole))
    {
        userGroup.emplace_back("ssh");
    }

    return isFoundAccountTypes;
}

inline void translateUserGroup(const std::vector<std::string>* userGroups,
                               crow::Response& res)
{
    if (userGroups == nullptr)
    {
        BMCWEB_LOG_ERROR << "userGroups wasn't a string vector";
        messages::internalError(res);
        return;
    }
    nlohmann::json& accountTypes = res.jsonValue["AccountTypes"];
    accountTypes = nlohmann::json::array();
    for (const auto& userGroup : *userGroups)
    {
        if (!getAccountTypeFromUserGroup(userGroup, accountTypes))
        {
            BMCWEB_LOG_ERROR << "mapped value not for this userGroup value : "
                             << userGroup;
            messages::internalError(res);
            return;
        }
    }
}

inline void translateAccountType(
    const std::optional<std::vector<std::string>>& accountType,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& dbusObjectPath, bool isUserItself)
{
    // user can not disable their own Redfish Property.
    if (isUserItself)
    {
        if (auto it = std::find(accountType->cbegin(), accountType->cend(),
                                "Redfish");
            it == accountType->cend())
        {
            BMCWEB_LOG_ERROR
                << "user can not disable their own Redfish Property";
            messages::strictAccountTypes(asyncResp->res, "AccountTypes");
            return;
        }
    }

    // MAP userGroup with accountTypes value
    std::vector<std::string> updatedUserGroup;
    if (!getUserGroupFromAccountType(asyncResp, accountType, updatedUserGroup))
    {
        BMCWEB_LOG_ERROR << "accountType value unable to mapped";
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
            return;
        },
        "xyz.openbmc_project.User.Manager", dbusObjectPath.c_str(),
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.User.Attributes", "UserGroups",
        dbus::utility::DbusVariantType{updatedUserGroup});
}

inline void userErrorMessageHandler(
    const sd_bus_error* e, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& newUser, const std::string& username)
{
    if (e == nullptr)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    const char* errorMessage = e->name;
    if (strcmp(errorMessage,
               "xyz.openbmc_project.User.Common.Error.UserNameExists") == 0)
    {
        messages::resourceAlreadyExists(asyncResp->res,
                                        "#ManagerAccount.v1_4_0.ManagerAccount",
                                        "UserName", newUser);
    }
    else if (strcmp(errorMessage, "xyz.openbmc_project.User.Common.Error."
                                  "UserNameDoesNotExist") == 0)
    {
        messages::resourceNotFound(
            asyncResp->res, "#ManagerAccount.v1_4_0.ManagerAccount", username);
    }
    else if ((strcmp(errorMessage,
                     "xyz.openbmc_project.Common.Error.InvalidArgument") ==
              0) ||
             (strcmp(errorMessage, "xyz.openbmc_project.User.Common.Error."
                                   "UserNameGroupFail") == 0))
    {
        messages::propertyValueFormatError(asyncResp->res, newUser, "UserName");
    }
    else if (strcmp(errorMessage,
                    "xyz.openbmc_project.User.Common.Error.NoResource") == 0)
    {
        messages::createLimitReachedForResource(asyncResp->res);
    }
    else
    {
        messages::internalError(asyncResp->res);
    }

    return;
}

inline void parseLDAPConfigData(nlohmann::json& jsonResponse,
                                const LDAPConfigData& confData,
                                const std::string& ldapType)
{
    std::string service =
        (ldapType == "LDAP") ? "LDAPService" : "ActiveDirectoryService";
    nlohmann::json ldap = {
        {"ServiceEnabled", confData.serviceEnabled},
        {"ServiceAddresses", nlohmann::json::array({confData.uri})},
        {"Authentication",
         {{"AuthenticationType", "UsernameAndPassword"},
          {"Username", confData.bindDN},
          {"Password", nullptr}}},
        {"LDAPService",
         {{"SearchSettings",
           {{"BaseDistinguishedNames",
             nlohmann::json::array({confData.baseDN})},
            {"UsernameAttribute", confData.userNameAttribute},
            {"GroupsAttribute", confData.groupAttribute}}}}},
    };

    jsonResponse[ldapType].update(ldap);

    nlohmann::json& roleMapArray = jsonResponse[ldapType]["RemoteRoleMapping"];
    roleMapArray = nlohmann::json::array();
    for (auto& obj : confData.groupRoleList)
    {
        BMCWEB_LOG_DEBUG << "Pushing the data groupName="
                         << obj.second.groupName << "\n";
        roleMapArray.push_back(
            {nlohmann::json::array({"RemoteGroup", obj.second.groupName}),
             nlohmann::json::array(
                 {"LocalRole", getRoleIdFromPrivilege(obj.second.privilege)})});
    }
}

/**
 *  @brief validates given JSON input and then calls appropriate method to
 * create, to delete or to set Rolemapping object based on the given input.
 *
 */
inline void handleRoleMapPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::pair<std::string, LDAPRoleMapData>>& roleMapObjData,
    const std::string& serverType, const std::vector<nlohmann::json>& input)
{
    for (size_t index = 0; index < input.size(); index++)
    {
        const nlohmann::json& thisJson = input[index];

        if (thisJson.is_null())
        {
            // delete the existing object
            if (index < roleMapObjData.size())
            {
                crow::connections::systemBus->async_method_call(
                    [asyncResp, roleMapObjData, serverType,
                     index](const boost::system::error_code ec) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR << "DBUS response error: " << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue[serverType]["RemoteRoleMapping"][index] =
                            nullptr;
                    },
                    ldapDbusService, roleMapObjData[index].first,
                    "xyz.openbmc_project.Object.Delete", "Delete");
            }
            else
            {
                BMCWEB_LOG_ERROR << "Can't delete the object";
                messages::propertyValueTypeError(
                    asyncResp->res,
                    thisJson.dump(2, ' ', true,
                                  nlohmann::json::error_handler_t::replace),
                    "RemoteRoleMapping/" + std::to_string(index));
                return;
            }
        }
        else if (thisJson.empty())
        {
            // Don't do anything for the empty objects,parse next json
            // eg {"RemoteRoleMapping",[{}]}
        }
        else
        {
            // update/create the object
            std::optional<std::string> remoteGroup;
            std::optional<std::string> localRole;

            // This is a copy, but it's required in this case because of how
            // readJson is structured
            nlohmann::json thisJsonCopy = thisJson;
            if (!json_util::readJson(thisJsonCopy, asyncResp->res,
                                     "RemoteGroup", remoteGroup, "LocalRole",
                                     localRole))
            {
                continue;
            }

            // Do not allow mapping to a Restricted LocalRole
            if (localRole && redfish::isRestrictedRole(*localRole))
            {
                messages::restrictedRole(asyncResp->res, *localRole);
                return;
            }

            // Update existing RoleMapping Object
            if (index < roleMapObjData.size())
            {
                BMCWEB_LOG_DEBUG << "Update Role Map Object";
                // If "RemoteGroup" info is provided
                if (remoteGroup)
                {
                    crow::connections::systemBus->async_method_call(
                        [asyncResp, roleMapObjData, serverType, index,
                         remoteGroup](const boost::system::error_code ec,
                                      const sdbusplus::message::message& msg) {
                            if (ec)
                            {
                                BMCWEB_LOG_ERROR << "DBUS response error: "
                                                 << ec;
                                const sd_bus_error* dbusError = msg.get_error();
                                if (dbusError == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                if ((strcmp(dbusError->name,
                                            "xyz.openbmc_project.Common.Error."
                                            "InvalidArgument") == 0))
                                {
                                    messages::propertyValueIncorrect(
                                        asyncResp->res, "RemoteGroup",
                                        *remoteGroup);
                                    return;
                                }
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            asyncResp->res
                                .jsonValue[serverType]["RemoteRoleMapping"]
                                          [index]["RemoteGroup"] = *remoteGroup;
                        },
                        ldapDbusService, roleMapObjData[index].first,
                        propertyInterface, "Set",
                        "xyz.openbmc_project.User.PrivilegeMapperEntry",
                        "GroupName",
                        std::variant<std::string>(std::move(*remoteGroup)));
                }

                // If "LocalRole" info is provided
                if (localRole)
                {
                    crow::connections::systemBus->async_method_call(
                        [asyncResp, roleMapObjData, serverType, index,
                         localRole](const boost::system::error_code ec,
                                    const sdbusplus::message::message& msg) {
                            if (ec)
                            {
                                BMCWEB_LOG_ERROR << "DBUS response error: "
                                                 << ec;
                                const sd_bus_error* dbusError = msg.get_error();
                                if (dbusError == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                if ((strcmp(dbusError->name,
                                            "xyz.openbmc_project.Common.Error."
                                            "InvalidArgument") == 0))
                                {
                                    messages::propertyValueIncorrect(
                                        asyncResp->res, "LocalRole",
                                        *localRole);
                                    return;
                                }
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            asyncResp->res
                                .jsonValue[serverType]["RemoteRoleMapping"]
                                          [index]["LocalRole"] = *localRole;
                        },
                        ldapDbusService, roleMapObjData[index].first,
                        propertyInterface, "Set",
                        "xyz.openbmc_project.User.PrivilegeMapperEntry",
                        "Privilege",
                        std::variant<std::string>(
                            getPrivilegeFromRoleId(std::move(*localRole))));
                }
            }
            // Create a new RoleMapping Object.
            else
            {
                BMCWEB_LOG_DEBUG
                    << "setRoleMappingProperties: Creating new Object";
                std::string pathString =
                    "RemoteRoleMapping/" + std::to_string(index);

                if (!localRole)
                {
                    messages::propertyMissing(asyncResp->res,
                                              pathString + "/LocalRole");
                    continue;
                }
                if (!remoteGroup)
                {
                    messages::propertyMissing(asyncResp->res,
                                              pathString + "/RemoteGroup");
                    continue;
                }

                std::string dbusObjectPath;
                if (serverType == "ActiveDirectory")
                {
                    dbusObjectPath = adConfigObject;
                }
                else if (serverType == "LDAP")
                {
                    dbusObjectPath = ldapConfigObjectName;
                }

                BMCWEB_LOG_DEBUG << "Remote Group=" << *remoteGroup
                                 << ",LocalRole=" << *localRole;

                crow::connections::systemBus->async_method_call(
                    [asyncResp, serverType, localRole,
                     remoteGroup](const boost::system::error_code ec,
                                  const sdbusplus::message::message& msg) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR << "DBUS response error: " << ec;
                            const sd_bus_error* dbusError = msg.get_error();
                            if (dbusError == nullptr)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            if ((strcmp(dbusError->name,
                                        "xyz.openbmc_project.Common.Error."
                                        "InvalidArgument") == 0))
                            {
                                messages::propertyValueIncorrect(
                                    asyncResp->res, "RemoteRoleMapping",
                                    *localRole);
                                return;
                            }
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        nlohmann::json& remoteRoleJson =
                            asyncResp->res
                                .jsonValue[serverType]["RemoteRoleMapping"];
                        remoteRoleJson.push_back(
                            {{"LocalRole", *localRole},
                             {"RemoteGroup", *remoteGroup}});
                    },
                    ldapDbusService, dbusObjectPath, ldapPrivMapperInterface,
                    "Create", *remoteGroup,
                    getPrivilegeFromRoleId(std::move(*localRole)));
            }
        }
    }
}

/**
 * Function that retrieves all properties for LDAP config object
 * into JSON
 */
template <typename CallbackFunc>
inline void getLDAPConfigData(const std::string& ldapType,
                              CallbackFunc&& callback)
{

    const std::array<const char*, 2> interfaces = {ldapEnableInterface,
                                                   ldapConfigInterface};

    crow::connections::systemBus->async_method_call(
        [callback, ldapType](const boost::system::error_code ec,
                             const GetObjectType& resp) {
            if (ec || resp.empty())
            {
                BMCWEB_LOG_ERROR << "DBUS response error during getting of "
                                    "service name: "
                                 << ec;
                LDAPConfigData empty{};
                callback(false, empty, ldapType);
                return;
            }
            std::string service = resp.begin()->first;
            crow::connections::systemBus->async_method_call(
                [callback, ldapType](const boost::system::error_code errorCode,
                                     const ManagedObjectType& ldapObjects) {
                    LDAPConfigData confData{};
                    if (errorCode)
                    {
                        callback(false, confData, ldapType);
                        BMCWEB_LOG_ERROR << "D-Bus responses error: "
                                         << errorCode;
                        return;
                    }

                    std::string ldapDbusType;
                    std::string searchString;

                    if (ldapType == "LDAP")
                    {
                        ldapDbusType = "xyz.openbmc_project.User.Ldap.Config."
                                       "Type.OpenLdap";
                        searchString = "openldap";
                    }
                    else if (ldapType == "ActiveDirectory")
                    {
                        ldapDbusType =
                            "xyz.openbmc_project.User.Ldap.Config.Type."
                            "ActiveDirectory";
                        searchString = "active_directory";
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR
                            << "Can't get the DbusType for the given type="
                            << ldapType;
                        callback(false, confData, ldapType);
                        return;
                    }

                    std::string ldapEnableInterfaceStr = ldapEnableInterface;
                    std::string ldapConfigInterfaceStr = ldapConfigInterface;

                    for (const auto& object : ldapObjects)
                    {
                        // let's find the object whose ldap type is equal to the
                        // given type
                        if (object.first.str.find(searchString) ==
                            std::string::npos)
                        {
                            continue;
                        }

                        for (const auto& interface : object.second)
                        {
                            if (interface.first == ldapEnableInterfaceStr)
                            {
                                // rest of the properties are string.
                                for (const auto& property : interface.second)
                                {
                                    if (property.first == "Enabled")
                                    {
                                        const bool* value =
                                            std::get_if<bool>(&property.second);
                                        if (value == nullptr)
                                        {
                                            continue;
                                        }
                                        confData.serviceEnabled = *value;
                                        break;
                                    }
                                }
                            }
                            else if (interface.first == ldapConfigInterfaceStr)
                            {

                                for (const auto& property : interface.second)
                                {
                                    const std::string* strValue =
                                        std::get_if<std::string>(
                                            &property.second);
                                    if (strValue == nullptr)
                                    {
                                        continue;
                                    }
                                    if (property.first == "LDAPServerURI")
                                    {
                                        confData.uri = *strValue;
                                    }
                                    else if (property.first == "LDAPBindDN")
                                    {
                                        confData.bindDN = *strValue;
                                    }
                                    else if (property.first == "LDAPBaseDN")
                                    {
                                        confData.baseDN = *strValue;
                                    }
                                    else if (property.first ==
                                             "LDAPSearchScope")
                                    {
                                        confData.searchScope = *strValue;
                                    }
                                    else if (property.first ==
                                             "GroupNameAttribute")
                                    {
                                        confData.groupAttribute = *strValue;
                                    }
                                    else if (property.first ==
                                             "UserNameAttribute")
                                    {
                                        confData.userNameAttribute = *strValue;
                                    }
                                    else if (property.first == "LDAPType")
                                    {
                                        confData.serverType = *strValue;
                                    }
                                }
                            }
                            else if (interface.first ==
                                     "xyz.openbmc_project.User."
                                     "PrivilegeMapperEntry")
                            {
                                LDAPRoleMapData roleMapData{};
                                for (const auto& property : interface.second)
                                {
                                    const std::string* strValue =
                                        std::get_if<std::string>(
                                            &property.second);

                                    if (strValue == nullptr)
                                    {
                                        continue;
                                    }

                                    if (property.first == "GroupName")
                                    {
                                        roleMapData.groupName = *strValue;
                                    }
                                    else if (property.first == "Privilege")
                                    {
                                        roleMapData.privilege = *strValue;
                                    }
                                }

                                confData.groupRoleList.emplace_back(
                                    object.first.str, roleMapData);
                            }
                        }
                    }
                    callback(true, confData, ldapType);
                },
                service, ldapRootObject, dbusObjManagerIntf,
                "GetManagedObjects");
        },
        mapperBusName, mapperObjectPath, mapperIntf, "GetObject",
        ldapConfigObjectName, interfaces);
}

/**
 * @brief parses the authentication section under the LDAP
 * @param input JSON data
 * @param asyncResp pointer to the JSON response
 * @param userName  userName to be filled from the given JSON.
 * @param password  password to be filled from the given JSON.
 */
inline void parseLDAPAuthenticationJson(
    nlohmann::json input, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::optional<std::string>& username, std::optional<std::string>& password)
{
    std::optional<std::string> authType;

    if (!json_util::readJson(input, asyncResp->res, "AuthenticationType",
                             authType, "Username", username, "Password",
                             password))
    {
        return;
    }
    if (!authType)
    {
        return;
    }
    if (*authType != "UsernameAndPassword")
    {
        messages::propertyValueNotInList(asyncResp->res, *authType,
                                         "AuthenticationType");
        return;
    }
}
/**
 * @brief parses the LDAPService section under the LDAP
 * @param input JSON data
 * @param asyncResp pointer to the JSON response
 * @param baseDNList baseDN to be filled from the given JSON.
 * @param userNameAttribute  userName to be filled from the given JSON.
 * @param groupaAttribute  password to be filled from the given JSON.
 */

inline void
    parseLDAPServiceJson(nlohmann::json input,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         std::optional<std::vector<std::string>>& baseDNList,
                         std::optional<std::string>& userNameAttribute,
                         std::optional<std::string>& groupsAttribute)
{
    std::optional<nlohmann::json> searchSettings;

    if (!json_util::readJson(input, asyncResp->res, "SearchSettings",
                             searchSettings))
    {
        return;
    }
    if (!searchSettings)
    {
        return;
    }
    if (!json_util::readJson(*searchSettings, asyncResp->res,
                             "BaseDistinguishedNames", baseDNList,
                             "UsernameAttribute", userNameAttribute,
                             "GroupsAttribute", groupsAttribute))
    {
        return;
    }
}
/**
 * @brief updates the LDAP server address and updates the
          json response with the new value.
 * @param serviceAddressList address to be updated.
 * @param asyncResp pointer to the JSON response
 * @param ldapServerElementName Type of LDAP
 server(openLDAP/ActiveDirectory)
 */

inline void handleServiceAddressPatch(
    const std::vector<std::string>& serviceAddressList,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ldapServerElementName,
    const std::string& ldapConfigObject)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, ldapServerElementName,
         serviceAddressList](const boost::system::error_code ec,
                             const sdbusplus::message::message& msg) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG
                    << "Error Occurred in updating the service address";
                const sd_bus_error* dbusError = msg.get_error();
                if (dbusError == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                if ((strcmp(
                         dbusError->name,
                         "xyz.openbmc_project.Common.Error.InvalidArgument") ==
                     0))
                {
                    messages::propertyValueIncorrect(
                        asyncResp->res, "ServiceAddresses",
                        serviceAddressList.front());
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            std::vector<std::string> modifiedserviceAddressList = {
                serviceAddressList.front()};
            asyncResp->res
                .jsonValue[ldapServerElementName]["ServiceAddresses"] =
                modifiedserviceAddressList;
            if ((serviceAddressList).size() > 1)
            {
                messages::propertyValueModified(asyncResp->res,
                                                "ServiceAddresses",
                                                serviceAddressList.front());
            }
            BMCWEB_LOG_DEBUG << "Updated the service address";
        },
        ldapDbusService, ldapConfigObject, propertyInterface, "Set",
        ldapConfigInterface, "LDAPServerURI",
        std::variant<std::string>(serviceAddressList.front()));
}
/**
 * @brief updates the LDAP Bind DN and updates the
          json response with the new value.
 * @param username name of the user which needs to be updated.
 * @param asyncResp pointer to the JSON response
 * @param ldapServerElementName Type of LDAP
 server(openLDAP/ActiveDirectory)
 */

inline void
    handleUserNamePatch(const std::string& username,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& ldapServerElementName,
                        const std::string& ldapConfigObject)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, username,
         ldapServerElementName](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "Error occurred in updating the username";
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue[ldapServerElementName]["Authentication"]
                                    ["Username"] = username;
            BMCWEB_LOG_DEBUG << "Updated the username";
        },
        ldapDbusService, ldapConfigObject, propertyInterface, "Set",
        ldapConfigInterface, "LDAPBindDN", std::variant<std::string>(username));
}

/**
 * @brief updates the LDAP password
 * @param password : ldap password which needs to be updated.
 * @param asyncResp pointer to the JSON response
 * @param ldapServerElementName Type of LDAP
 *        server(openLDAP/ActiveDirectory)
 */

inline void
    handlePasswordPatch(const std::string& password,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& ldapServerElementName,
                        const std::string& ldapConfigObject)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, password,
         ldapServerElementName](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "Error occurred in updating the password";
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue[ldapServerElementName]["Authentication"]
                                    ["Password"] = "";
            BMCWEB_LOG_DEBUG << "Updated the password";
        },
        ldapDbusService, ldapConfigObject, propertyInterface, "Set",
        ldapConfigInterface, "LDAPBindDNPassword",
        std::variant<std::string>(password));
}

/**
 * @brief updates the LDAP BaseDN and updates the
          json response with the new value.
 * @param baseDNList baseDN list which needs to be updated.
 * @param asyncResp pointer to the JSON response
 * @param ldapServerElementName Type of LDAP
 server(openLDAP/ActiveDirectory)
 */

inline void
    handleBaseDNPatch(const std::vector<std::string>& baseDNList,
                      const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& ldapServerElementName,
                      const std::string& ldapConfigObject)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, baseDNList,
         ldapServerElementName](const boost::system::error_code ec,
                                const sdbusplus::message::message& msg) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "Error Occurred in Updating the base DN";
                const sd_bus_error* dbusError = msg.get_error();
                if (dbusError == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                if ((strcmp(
                         dbusError->name,
                         "xyz.openbmc_project.Common.Error.InvalidArgument") ==
                     0))
                {
                    messages::propertyValueIncorrect(asyncResp->res,
                                                     "BaseDistinguishedNames",
                                                     baseDNList.front());
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            auto& serverTypeJson =
                asyncResp->res.jsonValue[ldapServerElementName];
            auto& searchSettingsJson =
                serverTypeJson["LDAPService"]["SearchSettings"];
            std::vector<std::string> modifiedBaseDNList = {baseDNList.front()};
            searchSettingsJson["BaseDistinguishedNames"] = modifiedBaseDNList;
            if (baseDNList.size() > 1)
            {
                messages::propertyValueModified(asyncResp->res,
                                                "BaseDistinguishedNames",
                                                baseDNList.front());
            }
            BMCWEB_LOG_DEBUG << "Updated the base DN";
        },
        ldapDbusService, ldapConfigObject, propertyInterface, "Set",
        ldapConfigInterface, "LDAPBaseDN",
        std::variant<std::string>(baseDNList.front()));
}
/**
 * @brief updates the LDAP user name attribute and updates the
          json response with the new value.
 * @param userNameAttribute attribute to be updated.
 * @param asyncResp pointer to the JSON response
 * @param ldapServerElementName Type of LDAP
 server(openLDAP/ActiveDirectory)
 */

inline void
    handleUserNameAttrPatch(const std::string& userNameAttribute,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& ldapServerElementName,
                            const std::string& ldapConfigObject)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, userNameAttribute,
         ldapServerElementName](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "Error Occurred in Updating the "
                                    "username attribute";
                messages::internalError(asyncResp->res);
                return;
            }
            auto& serverTypeJson =
                asyncResp->res.jsonValue[ldapServerElementName];
            auto& searchSettingsJson =
                serverTypeJson["LDAPService"]["SearchSettings"];
            searchSettingsJson["UsernameAttribute"] = userNameAttribute;
            BMCWEB_LOG_DEBUG << "Updated the user name attr.";
        },
        ldapDbusService, ldapConfigObject, propertyInterface, "Set",
        ldapConfigInterface, "UserNameAttribute",
        std::variant<std::string>(userNameAttribute));
}

inline void setPropertyAllowUnauthACFUpload(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, bool allow)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, allow](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
            return;
        },
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/ibmacf/allow_unauth_upload",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Object.Enable", "Enabled",
        std::variant<bool>(allow));
}

inline void getAcfProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::tuple<std::vector<uint8_t>, bool, std::string>& messageFDbus)
{
    asyncResp->res.jsonValue["Oem"]["IBM"]["@odata.type"] =
        "#OemManagerAccount.v1_0_0.IBM";
    asyncResp->res.jsonValue["Oem"]["IBM"]["ACF"]["@odata.type"] =
        "#OemManagerAccount.v1_0_0.ACF";
    // Get messages from call to InstallACF and add values to json
    std::vector<uint8_t> acfFile = std::get<0>(messageFDbus);
    std::string decodeACFFile(acfFile.begin(), acfFile.end());
    std::string encodedACFFile = crow::utility::base64encode(decodeACFFile);

    bool acfInstalled = std::get<1>(messageFDbus);
    std::string expirationDate = std::get<2>(messageFDbus);

    asyncResp->res
        .jsonValue["Oem"]["IBM"]["ACF"]["WarningLongDatedExpiration"] = nullptr;
    asyncResp->res.jsonValue["Oem"]["IBM"]["ACF"]["ACFFile"] = nullptr;
    asyncResp->res.jsonValue["Oem"]["IBM"]["ACF"]["ExpirationDate"] = nullptr;

    if (acfInstalled)
    {
        asyncResp->res.jsonValue["Oem"]["IBM"]["ACF"]["ExpirationDate"] =
            expirationDate;

        asyncResp->res.jsonValue["Oem"]["IBM"]["ACF"]["ACFFile"] =
            encodedACFFile;

        std::time_t result = std::time(nullptr);

        // YYYY-MM-DD format
        // Parse expirationDate to get difference between now and expiration
        std::string expirationDateCpy = expirationDate;
        std::string delimiter = "-";
        std::vector<int> parseTime;

        char* endPtr;
        size_t pos = 0;
        std::string token;
        // expirationDate should be exactly 10 characters
        if (expirationDateCpy.length() != 10)
        {
            BMCWEB_LOG_ERROR << "expirationDate format invalid";
            return;
        }
        while ((pos = expirationDateCpy.find(delimiter)) != std::string::npos)
        {
            token = expirationDateCpy.substr(0, pos);
            parseTime.push_back(
                static_cast<int>(std::strtol(token.c_str(), &endPtr, 10)));

            if (*endPtr != '\0')
            {
                BMCWEB_LOG_ERROR << "expirationDate format enum";
                return;
            }
            expirationDateCpy.erase(0, pos + delimiter.length());
        }
        parseTime.push_back(static_cast<int>(
            std::strtol(expirationDateCpy.c_str(), &endPtr, 10)));

        // Expect 3 sections. YYYY MM DD
        if (*endPtr != '\0' && parseTime.size() != 3)
        {
            BMCWEB_LOG_ERROR << "expirationDate format invalid";
            messages::internalError(asyncResp->res);
            return;
        }

        std::tm tm{}; // zero initialise
        tm.tm_year = parseTime.at(0) - 1900;
        tm.tm_mon = parseTime.at(1) - 1;
        tm.tm_mday = parseTime.at(2);

        std::time_t t = std::mktime(&tm);
        u_int diffTime = static_cast<u_int>(std::difftime(t, result));
        // BMC date is displayed if exp date > 30 days
        // 30 days = 30 * 24 * 60 * 60 seconds
        if (diffTime > 2592000)
        {
            asyncResp->res
                .jsonValue["Oem"]["IBM"]["ACF"]["WarningLongDatedExpiration"] =
                true;
        }
        else
        {
            asyncResp->res
                .jsonValue["Oem"]["IBM"]["ACF"]["WarningLongDatedExpiration"] =
                false;
        }
    }
    asyncResp->res.jsonValue["Oem"]["IBM"]["ACF"]["ACFInstalled"] =
        acfInstalled;

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const std::variant<bool>& retval) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            const bool* allowed = std::get_if<bool>(&retval);
            if (allowed == nullptr)
            {
                BMCWEB_LOG_ERROR << "Property 'allowed' is not bool";
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res
                .jsonValue["Oem"]["IBM"]["ACF"]["AllowUnauthACFUpload"] =
                *allowed;
        },
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/ibmacf/allow_unauth_upload",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Object.Enable", "Enabled");
}

/**
 * @brief updates the LDAP group attribute and updates the
          json response with the new value.
 * @param groupsAttribute attribute to be updated.
 * @param asyncResp pointer to the JSON response
 * @param ldapServerElementName Type of LDAP
 server(openLDAP/ActiveDirectory)
 */

inline void handleGroupNameAttrPatch(
    const std::string& groupsAttribute,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ldapServerElementName,
    const std::string& ldapConfigObject)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, groupsAttribute,
         ldapServerElementName](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "Error Occurred in Updating the "
                                    "groupname attribute";
                messages::internalError(asyncResp->res);
                return;
            }
            auto& serverTypeJson =
                asyncResp->res.jsonValue[ldapServerElementName];
            auto& searchSettingsJson =
                serverTypeJson["LDAPService"]["SearchSettings"];
            searchSettingsJson["GroupsAttribute"] = groupsAttribute;
            BMCWEB_LOG_DEBUG << "Updated the groupname attr";
        },
        ldapDbusService, ldapConfigObject, propertyInterface, "Set",
        ldapConfigInterface, "GroupNameAttribute",
        std::variant<std::string>(groupsAttribute));
}
/**
 * @brief updates the LDAP service enable and updates the
          json response with the new value.
 * @param input JSON data.
 * @param asyncResp pointer to the JSON response
 * @param ldapServerElementName Type of LDAP
 server(openLDAP/ActiveDirectory)
 */

inline void handleServiceEnablePatch(
    bool serviceEnabled, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ldapServerElementName,
    const std::string& ldapConfigObject)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, serviceEnabled,
         ldapServerElementName](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG
                    << "Error Occurred in Updating the service enable";
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue[ldapServerElementName]["ServiceEnabled"] =
                serviceEnabled;
            BMCWEB_LOG_DEBUG << "Updated Service enable = " << serviceEnabled;
        },
        ldapDbusService, ldapConfigObject, propertyInterface, "Set",
        ldapEnableInterface, "Enabled", std::variant<bool>(serviceEnabled));
}

inline void
    handleAuthMethodsPatch(nlohmann::json& input,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::optional<bool> basicAuth;
    std::optional<bool> cookie;
    std::optional<bool> sessionToken;
    std::optional<bool> xToken;
    std::optional<bool> tls;

    if (!json_util::readJson(input, asyncResp->res, "BasicAuth", basicAuth,
                             "Cookie", cookie, "SessionToken", sessionToken,
                             "XToken", xToken, "TLS", tls))
    {
        BMCWEB_LOG_ERROR << "Cannot read values from AuthMethod tag";
        return;
    }

    // Make a copy of methods configuration
    persistent_data::AuthConfigMethods authMethodsConfig =
        persistent_data::SessionStore::getInstance().getAuthMethodsConfig();

    if (basicAuth)
    {
#ifndef BMCWEB_ENABLE_BASIC_AUTHENTICATION
        messages::actionNotSupported(
            asyncResp->res, "Setting BasicAuth when basic-auth feature "
                            "is disabled");
        return;
#endif
        authMethodsConfig.basic = *basicAuth;
    }

    if (cookie)
    {
#ifndef BMCWEB_ENABLE_COOKIE_AUTHENTICATION
        messages::actionNotSupported(asyncResp->res,
                                     "Setting Cookie when cookie-auth feature "
                                     "is disabled");
        return;
#endif
        authMethodsConfig.cookie = *cookie;
    }

    if (sessionToken)
    {
#ifndef BMCWEB_ENABLE_SESSION_AUTHENTICATION
        messages::actionNotSupported(
            asyncResp->res, "Setting SessionToken when session-auth feature "
                            "is disabled");
        return;
#endif
        authMethodsConfig.sessionToken = *sessionToken;
    }

    if (xToken)
    {
#ifndef BMCWEB_ENABLE_XTOKEN_AUTHENTICATION
        messages::actionNotSupported(asyncResp->res,
                                     "Setting XToken when xtoken-auth feature "
                                     "is disabled");
        return;
#endif
        authMethodsConfig.xtoken = *xToken;
    }

    if (tls)
    {
#ifndef BMCWEB_ENABLE_MUTUAL_TLS_AUTHENTICATION
        messages::actionNotSupported(asyncResp->res,
                                     "Setting TLS when mutual-tls-auth feature "
                                     "is disabled");
        return;
#endif
        authMethodsConfig.tls = *tls;
    }

    if (!authMethodsConfig.basic && !authMethodsConfig.cookie &&
        !authMethodsConfig.sessionToken && !authMethodsConfig.xtoken &&
        !authMethodsConfig.tls)
    {
        // Do not allow user to disable everything
        messages::actionNotSupported(asyncResp->res,
                                     "of disabling all available methods");
        return;
    }

    persistent_data::SessionStore::getInstance().updateAuthMethodsConfig(
        authMethodsConfig);
    // Save configuration immediately
    persistent_data::getConfig().writeData();

    messages::success(asyncResp->res);
}

/**
 * @brief Get the required values from the given JSON, validates the
 *        value and create the LDAP config object.
 * @param input JSON data
 * @param asyncResp pointer to the JSON response
 * @param serverType Type of LDAP server(openLDAP/ActiveDirectory)
 */

inline void handleLDAPPatch(nlohmann::json& input,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& serverType)
{
    std::string dbusObjectPath;
    if (serverType == "ActiveDirectory")
    {
        dbusObjectPath = adConfigObject;
    }
    else if (serverType == "LDAP")
    {
        dbusObjectPath = ldapConfigObjectName;
    }
    else
    {
        return;
    }

    std::optional<nlohmann::json> authentication;
    std::optional<nlohmann::json> ldapService;
    std::optional<std::vector<std::string>> serviceAddressList;
    std::optional<bool> serviceEnabled;
    std::optional<std::vector<std::string>> baseDNList;
    std::optional<std::string> userNameAttribute;
    std::optional<std::string> groupsAttribute;
    std::optional<std::string> userName;
    std::optional<std::string> password;
    std::optional<std::vector<nlohmann::json>> remoteRoleMapData;

    if (!json_util::readJson(input, asyncResp->res, "Authentication",
                             authentication, "LDAPService", ldapService,
                             "ServiceAddresses", serviceAddressList,
                             "ServiceEnabled", serviceEnabled,
                             "RemoteRoleMapping", remoteRoleMapData))
    {
        return;
    }

    if (authentication)
    {
        parseLDAPAuthenticationJson(*authentication, asyncResp, userName,
                                    password);
    }
    if (ldapService)
    {
        parseLDAPServiceJson(*ldapService, asyncResp, baseDNList,
                             userNameAttribute, groupsAttribute);
    }
    if (serviceAddressList)
    {
        if ((*serviceAddressList).size() == 0)
        {
            messages::propertyValueNotInList(asyncResp->res, "[]",
                                             "ServiceAddress");
            return;
        }
    }
    if (baseDNList)
    {
        if ((*baseDNList).size() == 0)
        {
            messages::propertyValueNotInList(asyncResp->res, "[]",
                                             "BaseDistinguishedNames");
            return;
        }
    }

    // nothing to update, then return
    if (!userName && !password && !serviceAddressList && !baseDNList &&
        !userNameAttribute && !groupsAttribute && !serviceEnabled &&
        !remoteRoleMapData)
    {
        return;
    }

    // Get the existing resource first then keep modifying
    // whenever any property gets updated.
    getLDAPConfigData(serverType, [asyncResp, userName, password, baseDNList,
                                   userNameAttribute, groupsAttribute,
                                   serviceAddressList, serviceEnabled,
                                   dbusObjectPath, remoteRoleMapData](
                                      bool success,
                                      const LDAPConfigData& confData,
                                      const std::string& serverT) {
        if (!success)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        parseLDAPConfigData(asyncResp->res.jsonValue, confData, serverT);
        if (confData.serviceEnabled)
        {
            // Disable the service first and update the rest of
            // the properties.
            handleServiceEnablePatch(false, asyncResp, serverT, dbusObjectPath);
        }

        if (serviceAddressList)
        {
            handleServiceAddressPatch(*serviceAddressList, asyncResp, serverT,
                                      dbusObjectPath);
        }
        if (userName)
        {
            handleUserNamePatch(*userName, asyncResp, serverT, dbusObjectPath);
        }
        if (password)
        {
            handlePasswordPatch(*password, asyncResp, serverT, dbusObjectPath);
        }

        if (baseDNList)
        {
            handleBaseDNPatch(*baseDNList, asyncResp, serverT, dbusObjectPath);
        }
        if (userNameAttribute)
        {
            handleUserNameAttrPatch(*userNameAttribute, asyncResp, serverT,
                                    dbusObjectPath);
        }
        if (groupsAttribute)
        {
            handleGroupNameAttrPatch(*groupsAttribute, asyncResp, serverT,
                                     dbusObjectPath);
        }
        if (serviceEnabled)
        {
            // if user has given the value as true then enable
            // the service. if user has given false then no-op
            // as service is already stopped.
            if (*serviceEnabled)
            {
                handleServiceEnablePatch(*serviceEnabled, asyncResp, serverT,
                                         dbusObjectPath);
            }
        }
        else
        {
            // if user has not given the service enabled value
            // then revert it to the same state as it was
            // before.
            handleServiceEnablePatch(confData.serviceEnabled, asyncResp,
                                     serverT, dbusObjectPath);
        }

        if (remoteRoleMapData)
        {
            handleRoleMapPatch(asyncResp, confData.groupRoleList, serverT,
                               *remoteRoleMapData);
        }
    });
}

inline void updateUserProperties(
    std::shared_ptr<bmcweb::AsyncResp> asyncResp, const std::string& username,
    std::optional<std::string> password, std::optional<bool> enabled,
    std::optional<std::string> roleId, std::optional<bool> locked,
    std::optional<std::vector<std::string>> accountType, bool isUserItself)
{
    std::string dbusObjectPath = "/xyz/openbmc_project/user/" + username;
    dbus::utility::escapePathForDbus(dbusObjectPath);

    dbus::utility::checkDbusPathExists(
        dbusObjectPath,
        [dbusObjectPath, username, password(std::move(password)),
         roleId(std::move(roleId)), enabled, locked,
         accountType(std::move(accountType)), isUserItself,
         asyncResp{std::move(asyncResp)}](int rc) {
            if (!rc)
            {
                messages::resourceNotFound(
                    asyncResp->res, "#ManagerAccount.v1_4_0.ManagerAccount",
                    username);
                return;
            }

            if (password)
            {
                int retval = pamUpdatePassword(username, *password);

                if (retval == PAM_USER_UNKNOWN)
                {
                    messages::resourceNotFound(
                        asyncResp->res, "#ManagerAccount.v1_4_0.ManagerAccount",
                        username);
                }
                else if (retval == PAM_AUTHTOK_ERR)
                {
                    // If password is invalid
                    messages::propertyValueFormatError(asyncResp->res,
                                                       *password, "Password");
                    BMCWEB_LOG_ERROR << "pamUpdatePassword Failed";
                }
                else if (retval != PAM_SUCCESS)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
            }

            if (enabled)
            {
                crow::connections::systemBus->async_method_call(
                    [asyncResp](const boost::system::error_code ec) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        messages::success(asyncResp->res);
                        return;
                    },
                    "xyz.openbmc_project.User.Manager", dbusObjectPath.c_str(),
                    "org.freedesktop.DBus.Properties", "Set",
                    "xyz.openbmc_project.User.Attributes", "UserEnabled",
                    std::variant<bool>{*enabled});
            }

            if (roleId)
            {
                std::string priv = getPrivilegeFromRoleId(*roleId);
                if (priv.empty())
                {
                    messages::propertyValueNotInList(asyncResp->res, *roleId,
                                                     "RoleId");
                    return;
                }
                if (priv == "priv-noaccess")
                {
                    priv = "";
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp](const boost::system::error_code ec) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        messages::success(asyncResp->res);
                    },
                    "xyz.openbmc_project.User.Manager", dbusObjectPath.c_str(),
                    "org.freedesktop.DBus.Properties", "Set",
                    "xyz.openbmc_project.User.Attributes", "UserPrivilege",
                    std::variant<std::string>{priv});
            }

            if (locked)
            {
                // admin can unlock the account which is locked by
                // successive authentication failures but admin should
                // not be allowed to lock an account.
                if (*locked)
                {
                    messages::propertyValueNotInList(asyncResp->res, "true",
                                                     "Locked");
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp](const boost::system::error_code ec) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        messages::success(asyncResp->res);
                        return;
                    },
                    "xyz.openbmc_project.User.Manager", dbusObjectPath.c_str(),
                    "org.freedesktop.DBus.Properties", "Set",
                    "xyz.openbmc_project.User.Attributes",
                    "UserLockedForFailedAttempt", std::variant<bool>{*locked});
            }
            if (accountType)
            {
                translateAccountType(accountType, asyncResp, dbusObjectPath,
                                     isUserItself);
            }
        });
}

inline void uploadACF(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::vector<uint8_t>& decodedAcf)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    sdbusplus::message::message& m,
                    const std::tuple<std::vector<uint8_t>, bool, std::string>&
                        messageFDbus) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DBUS response error: " << ec;
                if (strcmp(m.get_error()->name,
                           "xyz.openbmc_project.Certs.Error."
                           "InvalidCertificate") == 0)
                {
                    redfish::messages::invalidUpload(
                        asyncResp->res,
                        "/redfish/v1/AccountService/Accounts/service",
                        "Invalid Certificate");
                }
                else
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            getAcfProperties(asyncResp, messageFDbus);
        },
        "xyz.openbmc_project.Certs.ACF."
        "Manager",
        "/xyz/openbmc_project/certs/ACF", "xyz.openbmc_project.Certs.ACF",
        "InstallACF", decodedAcf);
}

// This is called when someone either is not authenticated or is not
// authorized to upload an ACF, and they are trying to upload an ACF;
// in this condition, uploading an ACF is allowed when
// AllowUnauthACFUpload is true.
inline void triggerUnauthenticatedACFUpload(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::optional<nlohmann::json> oem)
{
    std::vector<uint8_t> decodedAcf;
    std::optional<nlohmann::json> ibm;
    if (!redfish::json_util::readJson(*oem, asyncResp->res, "IBM", ibm))
    {
        BMCWEB_LOG_ERROR << "Illegal Property ";
        messages::propertyMissing(asyncResp->res, "IBM");
        return;
    }

    if (ibm)
    {
        std::optional<nlohmann::json> acf;
        if (!redfish::json_util::readJson(*ibm, asyncResp->res, "ACF", acf))
        {
            BMCWEB_LOG_ERROR << "Illegal Property ";
            messages::propertyMissing(asyncResp->res, "ACF");
            return;
        }

        if (acf)
        {
            std::optional<std::string> acfFile;
            if (acf.value().contains("ACFFile") &&
                acf.value()["ACFFile"] == nullptr)
            {
                acfFile = "";
            }
            else
            {
                if (!redfish::json_util::readJson(*acf, asyncResp->res,
                                                  "ACFFile", acfFile))
                {
                    BMCWEB_LOG_ERROR << "Illegal Property ";
                    messages::propertyMissing(asyncResp->res, "ACFFile");
                    return;
                }

                std::string sDecodedAcf;
                if (!crow::utility::base64Decode(*acfFile, sDecodedAcf))
                {
                    BMCWEB_LOG_ERROR << "base64 decode failure ";
                    messages::internalError(asyncResp->res);
                    return;
                }
                try
                {
                    std::copy(sDecodedAcf.begin(), sDecodedAcf.end(),
                              std::back_inserter(decodedAcf));
                }
                catch (const std::exception& e)
                {
                    BMCWEB_LOG_ERROR << e.what();
                    messages::internalError(asyncResp->res);
                    return;
                }
            }
        }
    }

    // Allow ACF upload when D-Bus property allow_unauth_upload is true (aka
    // Redfish property AllowUnauthACFUpload).
    crow::connections::systemBus->async_method_call(
        [asyncResp, decodedAcf](const boost::system::error_code ec,
                                const std::variant<bool>& allowed) {
            if (ec)
            {
                BMCWEB_LOG_ERROR
                    << "D-Bus response error reading allow_unauth_upload: "
                    << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            const bool* allowUnauthACFUpload = std::get_if<bool>(&allowed);
            if (allowUnauthACFUpload == nullptr)
            {
                BMCWEB_LOG_ERROR << "nullptr for allow_unauth_upload";
                messages::internalError(asyncResp->res);
                return;
            }
            if (*allowUnauthACFUpload == true)
            {
                uploadACF(asyncResp, decodedAcf);
                return;
            }

            // Allow ACF upload when D-Bus property ACFWindowActive is true
            // (aka OpPanel function 74).
            crow::connections::systemBus->async_method_call(
                [asyncResp, decodedAcf](const boost::system::error_code ec,
                                        const std::variant<bool>& retVal) {
                    bool isActive;
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR
                            << "Failed to read ACFWindowActive property";
                        // The Panel app doesn't run on simulated systems.
                        isActive = false;
                    }
                    else
                    {
                        const bool* isACFWindowActive =
                            std::get_if<bool>(&retVal);
                        if (isACFWindowActive == nullptr)
                        {
                            BMCWEB_LOG_ERROR << "nullptr for ACFWindowActive";
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        isActive = *isACFWindowActive;
                    }

                    if (isActive == true)
                    {
                        uploadACF(asyncResp, decodedAcf);
                        return;
                    }

                    BMCWEB_LOG_ERROR << "ACF upload not allowed";
                    messages::insufficientPrivilege(asyncResp->res);
                    return;
                },
                "com.ibm.PanelApp", "/com/ibm/panel_app",
                "org.freedesktop.DBus.Properties", "Get", "com.ibm.panel",
                "ACFWindowActive");
        },
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/ibmacf/allow_unauth_upload",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Object.Enable", "Enabled");
}

inline void requestAccountServiceRoutes(App& app)
{

    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/")
        .privileges(redfish::privileges::getAccountService)
        .methods(
            boost::beast::http::verb::get)([](const crow::Request& req,
                                              const std::shared_ptr<
                                                  bmcweb::AsyncResp>& asyncResp)
                                               -> void {
            const persistent_data::AuthConfigMethods& authMethodsConfig =
                persistent_data::SessionStore::getInstance()
                    .getAuthMethodsConfig();

            asyncResp->res.jsonValue = {
                {"@odata.id", "/redfish/v1/AccountService"},
                {"@odata.type", "#AccountService."
                                "v1_5_0.AccountService"},
                {"Id", "AccountService"},
                {"Name", "Account Service"},
                {"Description", "Account Service"},
                {"ServiceEnabled", true},
                {"MaxPasswordLength", 64},
                {"Accounts",
                 {{"@odata.id", "/redfish/v1/AccountService/Accounts"}}},
                {"Roles", {{"@odata.id", "/redfish/v1/AccountService/Roles"}}},
                {"Oem",
                 {{"OpenBMC",
                   {{"@odata.type", "#OemAccountService.v1_0_0.AccountService"},
                    {"@odata.id", "/redfish/v1/AccountService#/Oem/OpenBMC"},
                    {"AuthMethods",
                     {
                         {"BasicAuth", authMethodsConfig.basic},
                         {"SessionToken", authMethodsConfig.sessionToken},
                         {"XToken", authMethodsConfig.xtoken},
                         {"Cookie", authMethodsConfig.cookie},
                         {"TLS", authMethodsConfig.tls},
                     }}}}}}};
            // /redfish/v1/AccountService/LDAP/Certificates is something only
            // ConfigureManager can access then only display when the user has
            // permissions ConfigureManager
            Privileges effectiveUserPrivileges =
                redfish::getUserPrivileges(req.userRole);

            if (isOperationAllowedWithPrivileges({{"ConfigureManager"}},
                                                 effectiveUserPrivileges))
            {
                asyncResp->res.jsonValue["LDAP"] = {
                    {"Certificates",
                     {{"@odata.id",
                       "/redfish/v1/AccountService/LDAP/Certificates"}}}};
            }
            crow::connections::systemBus->async_method_call(
                [asyncResp](
                    const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string,
                                  std::variant<uint32_t, uint16_t, uint8_t>>>&
                        propertiesList) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    BMCWEB_LOG_DEBUG << "Got " << propertiesList.size()
                                     << "properties for AccountService";
                    for (const std::pair<std::string,
                                         std::variant<uint32_t, uint16_t,
                                                      uint8_t>>& property :
                         propertiesList)
                    {
                        if (property.first == "MinPasswordLength")
                        {
                            const uint8_t* value =
                                std::get_if<uint8_t>(&property.second);
                            if (value != nullptr)
                            {
                                asyncResp->res.jsonValue["MinPasswordLength"] =
                                    *value;
                            }
                        }
                        if (property.first == "AccountUnlockTimeout")
                        {
                            const uint32_t* value =
                                std::get_if<uint32_t>(&property.second);
                            if (value != nullptr)
                            {
                                asyncResp->res
                                    .jsonValue["AccountLockoutDuration"] =
                                    *value;
                            }
                        }
                        if (property.first == "MaxLoginAttemptBeforeLockout")
                        {
                            const uint16_t* value =
                                std::get_if<uint16_t>(&property.second);
                            if (value != nullptr)
                            {
                                asyncResp->res
                                    .jsonValue["AccountLockoutThreshold"] =
                                    *value;
                            }
                        }
                    }
                },
                "xyz.openbmc_project.User.Manager", "/xyz/openbmc_project/user",
                "org.freedesktop.DBus.Properties", "GetAll",
                "xyz.openbmc_project.User.AccountPolicy");

            auto callback = [asyncResp](bool success, LDAPConfigData& confData,
                                        const std::string& ldapType) {
                if (!success)
                {
                    return;
                }
                parseLDAPConfigData(asyncResp->res.jsonValue, confData,
                                    ldapType);
            };

            getLDAPConfigData("LDAP", callback);
            getLDAPConfigData("ActiveDirectory", callback);
        });

    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/")
        .privileges(redfish::privileges::patchAccountService)
        .methods(boost::beast::http::verb::patch)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) -> void {
                std::optional<uint32_t> unlockTimeout;
                std::optional<uint16_t> lockoutThreshold;
                std::optional<uint16_t> minPasswordLength;
                std::optional<uint16_t> maxPasswordLength;
                std::optional<nlohmann::json> ldapObject;
                std::optional<nlohmann::json> activeDirectoryObject;
                std::optional<nlohmann::json> oemObject;

                if (!json_util::readJson(
                        req, asyncResp->res, "AccountLockoutDuration",
                        unlockTimeout, "AccountLockoutThreshold",
                        lockoutThreshold, "MaxPasswordLength",
                        maxPasswordLength, "MinPasswordLength",
                        minPasswordLength, "LDAP", ldapObject,
                        "ActiveDirectory", activeDirectoryObject, "Oem",
                        oemObject))
                {
                    return;
                }

                if (minPasswordLength)
                {
                    messages::propertyNotWritable(asyncResp->res,
                                                  "MinPasswordLength");
                }

                if (maxPasswordLength)
                {
                    messages::propertyNotWritable(asyncResp->res,
                                                  "MaxPasswordLength");
                }

                if (ldapObject)
                {
                    handleLDAPPatch(*ldapObject, asyncResp, "LDAP");
                }

                if (std::optional<nlohmann::json> oemOpenBMCObject;
                    oemObject &&
                    json_util::readJson(*oemObject, asyncResp->res, "OpenBMC",
                                        oemOpenBMCObject))
                {
                    if (std::optional<nlohmann::json> authMethodsObject;
                        oemOpenBMCObject &&
                        json_util::readJson(*oemOpenBMCObject, asyncResp->res,
                                            "AuthMethods", authMethodsObject))
                    {
                        if (authMethodsObject)
                        {
                            handleAuthMethodsPatch(*authMethodsObject,
                                                   asyncResp);
                        }
                    }
                }

                if (activeDirectoryObject)
                {
                    handleLDAPPatch(*activeDirectoryObject, asyncResp,
                                    "ActiveDirectory");
                }

                if (unlockTimeout)
                {
                    crow::connections::systemBus->async_method_call(
                        [asyncResp](const boost::system::error_code ec) {
                            if (ec)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            messages::success(asyncResp->res);
                        },
                        "xyz.openbmc_project.User.Manager",
                        "/xyz/openbmc_project/user",
                        "org.freedesktop.DBus.Properties", "Set",
                        "xyz.openbmc_project.User.AccountPolicy",
                        "AccountUnlockTimeout",
                        std::variant<uint32_t>(*unlockTimeout));
                }
                if (lockoutThreshold)
                {
                    crow::connections::systemBus->async_method_call(
                        [asyncResp](const boost::system::error_code ec) {
                            if (ec)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            messages::success(asyncResp->res);
                        },
                        "xyz.openbmc_project.User.Manager",
                        "/xyz/openbmc_project/user",
                        "org.freedesktop.DBus.Properties", "Set",
                        "xyz.openbmc_project.User.AccountPolicy",
                        "MaxLoginAttemptBeforeLockout",
                        std::variant<uint16_t>(*lockoutThreshold));
                }
            });

    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/Accounts/")
        .privileges(redfish::privileges::getManagerAccountCollection)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) -> void {
                asyncResp->res.jsonValue = {
                    {"@odata.id", "/redfish/v1/AccountService/Accounts"},
                    {"@odata.type", "#ManagerAccountCollection."
                                    "ManagerAccountCollection"},
                    {"Name", "Accounts Collection"},
                    {"Description", "BMC User Accounts"}};

                Privileges effectiveUserPrivileges =
                    redfish::getUserPrivileges(req.userRole);

                std::string thisUser = req.session->username;

                crow::connections::systemBus->async_method_call(
                    [asyncResp, thisUser, effectiveUserPrivileges](
                        const boost::system::error_code ec,
                        const ManagedObjectType& users) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        bool userCanSeeAllAccounts =
                            effectiveUserPrivileges.isSupersetOf(
                                {"ConfigureUsers"});

                        bool userCanSeeSelf =
                            effectiveUserPrivileges.isSupersetOf(
                                {"ConfigureSelf"});

                        nlohmann::json& memberArray =
                            asyncResp->res.jsonValue["Members"];
                        memberArray = nlohmann::json::array();

                        for (auto& userpath : users)
                        {
                            std::string user = userpath.first.filename();
                            if (user.empty())
                            {
                                messages::internalError(asyncResp->res);
                                BMCWEB_LOG_ERROR << "Invalid firmware ID";

                                return;
                            }

                            // As clarified by Redfish here:
                            // https://redfishforum.com/thread/281/manageraccountcollection-change-allows-account-enumeration
                            // Users without ConfigureUsers, only see their own
                            // account. Users with ConfigureUsers, see all
                            // accounts.
                            if (userCanSeeAllAccounts ||
                                (thisUser == user && userCanSeeSelf))
                            {
                                memberArray.push_back(
                                    {{"@odata.id",
                                      "/redfish/v1/AccountService/Accounts/" +
                                          user}});
                            }
                        }
                        asyncResp->res.jsonValue["Members@odata.count"] =
                            memberArray.size();
                    },
                    "xyz.openbmc_project.User.Manager",
                    "/xyz/openbmc_project/user",
                    "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
            });

    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/Accounts/")
        .privileges(redfish::privileges::postManagerAccountCollection)
        .methods(boost::beast::http::verb::post)([](const crow::Request& req,
                                                    const std::shared_ptr<
                                                        bmcweb::AsyncResp>&
                                                        asyncResp) -> void {
            std::string username;
            std::string password;
            std::optional<std::string> roleId("User");
            std::optional<bool> enabled = true;
            if (!json_util::readJson(req, asyncResp->res, "UserName", username,
                                     "Password", password, "RoleId", roleId,
                                     "Enabled", enabled))
            {
                return;
            }

            // Don't allow new accounts to have a Restricted Role.
            if (redfish::isRestrictedRole(*roleId))
            {
                messages::restrictedRole(asyncResp->res, *roleId);
                return;
            }

            std::string priv = getPrivilegeFromRoleId(*roleId);
            if (priv.empty())
            {
                messages::propertyValueNotInList(asyncResp->res, *roleId,
                                                 "RoleId");
                return;
            }
            // TODO: Following override will be reverted once support in
            // phosphor-user-manager is added. In order to avoid dependency
            // issues, this is added in bmcweb, which will removed, once
            // phosphor-user-manager supports priv-noaccess.
            // WARNING: roleId changes from Redfish Role to Phosphor privilege
            // role.
            if (priv == "priv-noaccess")
            {
                roleId = "";
            }
            else
            {
                roleId = priv;
            }

            // Reading AllGroups property
            crow::connections::systemBus->async_method_call(
                [asyncResp, username, password{std::move(password)}, roleId,
                 enabled](
                    const boost::system::error_code ec,
                    const std::variant<std::vector<std::string>>& allGroups) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG << "ERROR with async_method_call";
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const std::vector<std::string>* allGroupsList =
                        std::get_if<std::vector<std::string>>(&allGroups);

                    if (allGroupsList == nullptr || allGroupsList->empty())
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    // Create (modified) modGroupsList from allGroupsList.
                    // Remove the ipmi group.  Also Remove "ssh" if the new
                    // user is not an Administrator.
                    std::vector<std::string> modGroupsList;

                    for (const auto& group : *allGroupsList)
                    {
                        if ((group != "ipmi") &&
                            ((group != "ssh") || (*roleId == "Administrator")))
                        {
                            modGroupsList.push_back(group);
                        }
                    }

                    const std::vector<std::string>* pModGroupsList =
                        &modGroupsList;

                    crow::connections::systemBus->async_method_call(
                        [asyncResp, username,
                         password](const boost::system::error_code ec2,
                                   sdbusplus::message::message& m) {
                            if (ec2)
                            {
                                userErrorMessageHandler(
                                    m.get_error(), asyncResp, username, "");
                                return;
                            }

                            if (pamUpdatePassword(username, password) !=
                                PAM_SUCCESS)
                            {
                                // At this point we have a user that's been
                                // created, but the password set
                                // failed.Something is wrong, so delete the user
                                // that we've already created
                                crow::connections::systemBus->async_method_call(
                                    [asyncResp, password](
                                        const boost::system::error_code ec3) {
                                        if (ec3)
                                        {
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }

                                        // If password is invalid
                                        messages::propertyValueFormatError(
                                            asyncResp->res, password,
                                            "Password");
                                    },
                                    "xyz.openbmc_project.User.Manager",
                                    "/xyz/openbmc_project/user/" + username,
                                    "xyz.openbmc_project.Object.Delete",
                                    "Delete");

                                BMCWEB_LOG_ERROR << "pamUpdatePassword Failed";
                                return;
                            }

                            messages::created(asyncResp->res);
                            asyncResp->res.addHeader(
                                "Location",
                                "/redfish/v1/AccountService/Accounts/" +
                                    username);
                        },
                        "xyz.openbmc_project.User.Manager",
                        "/xyz/openbmc_project/user",
                        "xyz.openbmc_project.User.Manager", "CreateUser",
                        username, *pModGroupsList, *roleId, *enabled);
                },
                "xyz.openbmc_project.User.Manager", "/xyz/openbmc_project/user",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.User.Manager", "AllGroups");
        });

    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/Accounts/<str>/")
        .privileges(redfish::privileges::getManagerAccount)
        .methods(
            boost::beast::http::verb::
                get)([](const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& accountName) -> void {
            if (req.session->username != accountName)
            {
                // At this point we've determined that the user is trying to
                // modify a user that isn't them.  We need to verify that they
                // have permissions to modify other users, so re-run the auth
                // check with the same permissions, minus ConfigureSelf.
                Privileges effectiveUserPrivileges =
                    redfish::getUserPrivileges(req.userRole);
                Privileges requiredPermissionsToChangeNonSelf = {
                    "ConfigureUsers", "ConfigureManager"};
                if (!effectiveUserPrivileges.isSupersetOf(
                        requiredPermissionsToChangeNonSelf))
                {
                    BMCWEB_LOG_DEBUG << "GET Account denied access";
                    messages::insufficientPrivilege(asyncResp->res);
                    return;
                }
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, accountName](const boost::system::error_code ec,
                                         const ManagedObjectType& users) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    auto userIt = users.begin();

                    for (; userIt != users.end(); userIt++)
                    {
                        if (boost::ends_with(userIt->first.str,
                                             "/" + accountName))
                        {
                            break;
                        }
                    }
                    if (userIt == users.end())
                    {
                        messages::resourceNotFound(
                            asyncResp->res, "ManagerAccount", accountName);
                        return;
                    }

                    asyncResp->res.jsonValue = {
                        {"@odata.type",
                         "#ManagerAccount.v1_7_0.ManagerAccount"},
                        {"Name", "User Account"},
                        {"Description", "User Account"},
                        {"Password", nullptr},
                        {"StrictAccountTypes", true}};

                    for (const auto& interface : userIt->second)
                    {
                        if (interface.first ==
                            "xyz.openbmc_project.User.Attributes")
                        {
                            for (const auto& property : interface.second)
                            {
                                if (property.first == "UserEnabled")
                                {
                                    const bool* userEnabled =
                                        std::get_if<bool>(&property.second);
                                    if (userEnabled == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR
                                            << "UserEnabled wasn't a bool";
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    asyncResp->res.jsonValue["Enabled"] =
                                        *userEnabled;
                                }
                                else if (property.first ==
                                         "UserLockedForFailedAttempt")
                                {
                                    const bool* userLocked =
                                        std::get_if<bool>(&property.second);
                                    if (userLocked == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR << "UserLockedForF"
                                                            "ailedAttempt "
                                                            "wasn't a bool";
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    asyncResp->res.jsonValue["Locked"] =
                                        *userLocked;
                                    asyncResp->res.jsonValue
                                        ["Locked@Redfish.AllowableValues"] = {
                                        "false"}; // can only unlock accounts
                                }
                                else if (property.first == "UserPrivilege")
                                {
                                    const std::string* userPrivPtr =
                                        std::get_if<std::string>(
                                            &property.second);
                                    if (userPrivPtr == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR
                                            << "UserPrivilege wasn't a "
                                               "string";
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    std::string role =
                                        getRoleIdFromPrivilege(*userPrivPtr);
                                    if (role.empty())
                                    {
                                        BMCWEB_LOG_ERROR << "Invalid user role";
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    asyncResp->res.jsonValue["RoleId"] = role;

                                    asyncResp->res.jsonValue["Links"]["Role"] =
                                        {{"@odata.id",
                                          "/redfish/v1/AccountService/"
                                          "Roles/" +
                                              role}};
                                }
                                else if (property.first ==
                                         "UserPasswordExpired")
                                {
                                    const bool* userPasswordExpired =
                                        std::get_if<bool>(&property.second);
                                    if (userPasswordExpired == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR << "UserPassword"
                                                            "Expired "
                                                            "wasn't a bool";
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    asyncResp->res
                                        .jsonValue["PasswordChangeRequired"] =
                                        *userPasswordExpired;
                                }
                                else if (property.first == "UserGroups")
                                {
                                    const std::vector<std::string>* userGroups =
                                        std::get_if<std::vector<std::string>>(
                                            &property.second);

                                    translateUserGroup(userGroups,
                                                       asyncResp->res);
                                }
                            }
                        }
                    }

                    asyncResp->res.jsonValue["@odata.id"] =
                        "/redfish/v1/AccountService/Accounts/" + accountName;
                    asyncResp->res.jsonValue["Id"] = accountName;
                    asyncResp->res.jsonValue["UserName"] = accountName;

                    if (accountName == "service")
                    {
                        crow::connections::systemBus->async_method_call(
                            [asyncResp](
                                const boost::system::error_code ec,
                                const std::tuple<std::vector<uint8_t>, bool,
                                                 std::string>& messageFDbus) {
                                if (ec)
                                {
                                    BMCWEB_LOG_ERROR << "DBUS response error: "
                                                     << ec;
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                getAcfProperties(asyncResp, messageFDbus);
                            },
                            "xyz.openbmc_project.Certs.ACF.Manager",
                            "/xyz/openbmc_project/certs/ACF",
                            "xyz.openbmc_project.Certs.ACF", "GetACFInfo");
                    }
                },
                "xyz.openbmc_project.User.Manager", "/xyz/openbmc_project/user",
                "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        });

    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/Accounts/<str>/")
        // TODO this privilege should be using the generated endpoints, but
        // because of the special handling of ConfigureSelf, it's not able to
        // yet
        .privileges({{"ConfigureUsers"}, {"ConfigureSelf"}})
        .methods(boost::beast::http::verb::patch)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& username) -> void {
                std::optional<std::string> newUserName;
                std::optional<std::string> password;
                std::optional<bool> enabled;
                std::optional<std::string> roleId;
                std::optional<bool> locked;
                std::optional<nlohmann::json> oem;
                std::optional<std::vector<std::string>> accountType;
                bool isUserItself = false;

                if (!json_util::readJson(req, asyncResp->res, "UserName",
                                         newUserName, "Password", password,
                                         "RoleId", roleId, "Enabled", enabled,
                                         "Locked", locked, "Oem", oem,
                                         "AccountTypes", accountType))
                {
                    return;
                }

                // Unauthenticated user
                if (req.session == nullptr)
                {
                    // If user is service
                    if (username == "service")
                    {
                        if (oem)
                        {
                            // allow unauthenticated ACF upload based on panel
                            // function 74 state.
                            triggerUnauthenticatedACFUpload(asyncResp, oem);
                            return;
                        }
                    }
                    messages::insufficientPrivilege(asyncResp->res);
                    return;
                }

                // check user isitself or not
                isUserItself = (username == req.session->username);

                Privileges effectiveUserPrivileges =
                    redfish::getUserPrivileges(req.userRole);
                Privileges configureUsers = {"ConfigureUsers"};
                bool userHasConfigureUsers =
                    effectiveUserPrivileges.isSupersetOf(configureUsers);
                if (!userHasConfigureUsers)
                {
                    // Irrespective of role can patch ACF if function
                    // 74 is active from panel.
                    if (oem && (username == "service"))
                    {
                        // allow unauthenticated ACF upload based on panel
                        // function 74 state.
                        triggerUnauthenticatedACFUpload(asyncResp, oem);
                        return;
                    }

                    // ConfigureSelf accounts can only modify their own account
                    if (username != req.session->username)
                    {
                        messages::insufficientPrivilege(asyncResp->res);
                        return;
                    }
                    // ConfigureSelf accounts can only modify their password
                    if (!json_util::readJson(req, asyncResp->res, "Password",
                                             password))
                    {
                        return;
                    }
                }

                // For accounts which have a Restricted Role, restrict which
                // properties can be patched.  Allow only Locked, Enabled, and
                // Oem. Do not even allow the service user to change these
                // properties. Implementation note: Ideally this would get the
                // user's RoleId but that would take an additional D-Bus
                // operation.
                if ((username == "service") &&
                    (newUserName || password || roleId))
                {
                    BMCWEB_LOG_ERROR
                        << "Attempt to PATCH user who has a Restricted Role";
                    messages::restrictedRole(asyncResp->res,
                                             "OemIBMServiceAgent");
                    return;
                }

                // Don't allow PATCHing an account to have a Restricted role.
                if (roleId && redfish::isRestrictedRole(*roleId))
                {
                    BMCWEB_LOG_ERROR
                        << "Attempt to PATCH user to have a Restricted Role";
                    messages::restrictedRole(asyncResp->res, *roleId);
                    return;
                }

                if (oem)
                {
                    if (username != "service")
                    {
                        // Only the service user has Oem properties
                        messages::propertyUnknown(asyncResp->res, "Oem");
                        return;
                    }

                    std::optional<nlohmann::json> ibm;
                    if (!redfish::json_util::readJson(*oem, asyncResp->res,
                                                      "IBM", ibm))
                    {
                        BMCWEB_LOG_ERROR << "Illegal Property ";
                        messages::propertyMissing(asyncResp->res, "IBM");
                        return;
                    }
                    if (ibm)
                    {
                        std::optional<nlohmann::json> acf;
                        if (!redfish::json_util::readJson(*ibm, asyncResp->res,
                                                          "ACF", acf))
                        {
                            BMCWEB_LOG_ERROR << "Illegal Property ";
                            messages::propertyMissing(asyncResp->res, "ACF");
                            return;
                        }
                        if (acf)
                        {
                            std::optional<bool> allowUnauthACFUpload;
                            std::optional<std::string> acfFile;
                            bool rc;
                            // Property ACFFile may be null or string
                            if (acf.value().contains("ACFFile") &&
                                (acf.value()["ACFFile"] == nullptr))
                            {
                                acfFile = "";
                                rc = true;
                            }
                            else
                            {
                                rc = redfish::json_util::readJson(
                                    *acf, asyncResp->res, "ACFFile", acfFile,
                                    "AllowUnauthACFUpload",
                                    allowUnauthACFUpload);
                            }
                            if (!rc)
                            {
                                BMCWEB_LOG_ERROR << "Illegal Property ";
                                messages::propertyMissing(asyncResp->res,
                                                          "ACFFile");
                                messages::propertyMissing(
                                    asyncResp->res, "AllowUnauthACFUpload");
                                return;
                            }

                            if (acfFile)
                            {
                                std::vector<uint8_t> decodedAcf;
                                std::string sDecodedAcf;
                                if (!crow::utility::base64Decode(*acfFile,
                                                                 sDecodedAcf))
                                {
                                    BMCWEB_LOG_ERROR
                                        << "base64 decode failure ";
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                try
                                {
                                    std::copy(sDecodedAcf.begin(),
                                              sDecodedAcf.end(),
                                              std::back_inserter(decodedAcf));
                                }
                                catch (const std::exception& e)
                                {
                                    BMCWEB_LOG_ERROR << e.what();
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                uploadACF(asyncResp, decodedAcf);
                            }

                            if (allowUnauthACFUpload)
                            {
                                setPropertyAllowUnauthACFUpload(
                                    asyncResp, *allowUnauthACFUpload);
                            }
                        }
                    }
                }

                // if user name is not provided in the patch method or if it
                // matches the user name in the URI, then we are treating it as
                // updating user properties other then username. If username
                // provided doesn't match the URI, then we are treating this as
                // user rename request.
                if (!newUserName || (newUserName.value() == username))
                {
                    updateUserProperties(asyncResp, username, password, enabled,
                                         roleId, locked, accountType,
                                         isUserItself);
                    return;
                }
                crow::connections::systemBus->async_method_call(
                    [asyncResp, username, password(std::move(password)),
                     roleId(std::move(roleId)), enabled,
                     newUser{std::string(*newUserName)}, locked, isUserItself,
                     accountType{std::move(accountType)}](
                        const boost::system::error_code ec,
                        sdbusplus::message::message& m) {
                        if (ec)
                        {
                            userErrorMessageHandler(m.get_error(), asyncResp,
                                                    newUser, username);
                            return;
                        }

                        updateUserProperties(asyncResp, newUser, password,
                                             enabled, roleId, locked,
                                             accountType, isUserItself);
                    },
                    "xyz.openbmc_project.User.Manager",
                    "/xyz/openbmc_project/user",
                    "xyz.openbmc_project.User.Manager", "RenameUser", username,
                    *newUserName);
            });

    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/Accounts/<str>/")
        .privileges(redfish::privileges::deleteManagerAccount)
        .methods(boost::beast::http::verb::delete_)(
            [](const crow::Request& /*req*/,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& username) -> void {
                const std::string userPath =
                    "/xyz/openbmc_project/user/" + username;

                // Don't DELETE accounts which have a Restricted Role (the
                // service account). Implementation note: Ideally this would get
                // the user's RoleId but that would take an additional D-Bus
                // operation.
                if (username == "service")
                {
                    BMCWEB_LOG_ERROR
                        << "Attempt to DELETE user who has a Restricted Role";
                    messages::restrictedRole(asyncResp->res,
                                             "OemIBMServiceAgent");
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp, username](const boost::system::error_code ec) {
                        if (ec)
                        {
                            messages::resourceNotFound(
                                asyncResp->res,
                                "#ManagerAccount.v1_4_0.ManagerAccount",
                                username);
                            return;
                        }

                        messages::accountRemoved(asyncResp->res);
                    },
                    "xyz.openbmc_project.User.Manager", userPath,
                    "xyz.openbmc_project.Object.Delete", "Delete");
            });
}

} // namespace redfish
