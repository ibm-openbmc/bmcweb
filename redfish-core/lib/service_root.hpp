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
#include <persistent_data.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/systemd_utils.hpp>

#include <filesystem>

namespace redfish
{

inline void
    handleACFWindowActive(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Redfish property ACFWindowActive = true when D-Bus property
    // allow_unauth_upload is true (Redfish property AllowUnauthACFUpload).
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec, 
                    const std::variant<bool>& allowed) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus response error reading allow_unauth_upload: " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            const bool *allowUnauthACFUpload = std::get_if<bool>(&allowed);
            if (allowUnauthACFUpload == nullptr)
            {
                BMCWEB_LOG_ERROR << "nullptr for allow_unauth_upload";
                messages::internalError(asyncResp->res);
                return;
            }
            if (*allowUnauthACFUpload == true)
            {
                asyncResp->res.jsonValue["Oem"]["IBM"]["ACFWindowActive"] =
                    *allowUnauthACFUpload;
                return;
            }

            // ACFWindowActive = true also when D-Bus property ACFWindowActive
            // is true (OpPanel function 74).  Otherwise, the Window is not
            // active.
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
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
                        const bool* isACFWindowActive = std::get_if<bool>(&retVal);
                        if (isACFWindowActive == nullptr)
                        {
                            BMCWEB_LOG_ERROR << "nullptr for ACFWindowActive";
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        isActive = *isACFWindowActive;
                    }

                    asyncResp->res.jsonValue["Oem"]["IBM"]["ACFWindowActive"] =
                        isActive;
                },
                "com.ibm.PanelApp", "/com/ibm/panel_app",
                "org.freedesktop.DBus.Properties", "Get", "com.ibm.panel",
                "ACFWindowActive");
        },
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/ibmacf/allow_unauth_upload",
        "org.freedesktop.DBus.Properties",
        "Get",
        "xyz.openbmc_project.Object.Enable",
        "Enabled");
}

inline void
    handleServiceRootOem(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(asyncResp->res);
                return;
            }
            // Iterate over all retrieved ObjectPaths.
            for (const auto& object : subtree)
            {
                const std::string& path = object.first;
                BMCWEB_LOG_DEBUG << "Got path: " << path;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;
                if (connectionNames.empty())
                {
                    continue;
                }

                // This is not system, so check if it's cpu, dimm, UUID or
                // BiosVer
                for (const auto& connection : connectionNames)
                {
                    for (const auto& interfaceName : connection.second)
                    {
                        if (interfaceName ==
                            "xyz.openbmc_project.Inventory.Item.System")
                        {
                            crow::connections::systemBus->async_method_call(
                                [asyncResp](
                                    const boost::system::error_code ec2,
                                    const std::vector<
                                        std::pair<std::string, VariantType>>&
                                        propertiesList) {
                                    if (ec2)
                                    {
                                        // doesn't have to include this
                                        // interface
                                        return;
                                    }
                                    BMCWEB_LOG_DEBUG
                                        << "Got " << propertiesList.size()
                                        << " properties for system";
                                    for (const std::pair<std::string,
                                                         VariantType>&
                                             property : propertiesList)
                                    {
                                        const std::string propertyName =
                                            property.first;
                                        if ((propertyName == "Model") ||
                                            (propertyName == "SerialNumber"))
                                        {
                                            const std::string* value =
                                                std::get_if<std::string>(
                                                    &property.second);
                                            if (value != nullptr)
                                            {
                                                asyncResp->res
                                                    .jsonValue["Oem"]["IBM"]
                                                              [propertyName] =
                                                    *value;
                                            }
                                        }
                                    }
                                },
                                connection.first, path,
                                "org.freedesktop.DBus.Properties", "GetAll",
                                "xyz.openbmc_project.Inventory.Decorator."
                                "Asset");
                        }
                    }
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Decorator.Asset",
        });

    std::pair<std::string, std::string> redfishDateTimeOffset =
        crow::utility::getDateTimeOffsetNow();

    asyncResp->res.jsonValue["Oem"]["IBM"]["DateTime"] =
        redfishDateTimeOffset.first;
    asyncResp->res.jsonValue["Oem"]["IBM"]["DateTimeLocalOffset"] =
        redfishDateTimeOffset.second;
    handleACFWindowActive(asyncResp);
    asyncResp->res.jsonValue["Oem"]["@odata.type"] = "#OemServiceRoot.Oem";
    asyncResp->res.jsonValue["Oem"]["IBM"]["@odata.type"] =
        "#OemServiceRoot.IBM";
}

inline void
    handleServiceRootGet(const crow::Request&,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{

    std::string uuid = persistent_data::getConfig().systemUuid;
    asyncResp->res.jsonValue["@odata.type"] =
        "#ServiceRoot.v1_12_0.ServiceRoot";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1";
    asyncResp->res.jsonValue["Id"] = "RootService";
    asyncResp->res.jsonValue["Name"] = "Root Service";
    asyncResp->res.jsonValue["RedfishVersion"] = "1.12.0";
    asyncResp->res.jsonValue["Links"]["Sessions"] = {
        {"@odata.id", "/redfish/v1/SessionService/Sessions"}};
    asyncResp->res.jsonValue["AccountService"] = {
        {"@odata.id", "/redfish/v1/AccountService"}};
    asyncResp->res.jsonValue["Chassis"] = {
        {"@odata.id", "/redfish/v1/Chassis"}};
    asyncResp->res.jsonValue["JsonSchemas"] = {
        {"@odata.id", "/redfish/v1/JsonSchemas"}};
    asyncResp->res.jsonValue["Managers"] = {
        {"@odata.id", "/redfish/v1/Managers"}};
    asyncResp->res.jsonValue["SessionService"] = {
        {"@odata.id", "/redfish/v1/SessionService"}};
    asyncResp->res.jsonValue["Systems"] = {
        {"@odata.id", "/redfish/v1/Systems"}};
    asyncResp->res.jsonValue["Registries"] = {
        {"@odata.id", "/redfish/v1/Registries"}};

    asyncResp->res.jsonValue["UpdateService"] = {
        {"@odata.id", "/redfish/v1/UpdateService"}};
    asyncResp->res.jsonValue["UUID"] = uuid;
    asyncResp->res.jsonValue["CertificateService"] = {
        {"@odata.id", "/redfish/v1/CertificateService"}};
    asyncResp->res.jsonValue["Tasks"] = {
        {"@odata.id", "/redfish/v1/TaskService"}};
    asyncResp->res.jsonValue["EventService"] = {
        {"@odata.id", "/redfish/v1/EventService"}};
    asyncResp->res.jsonValue["TelemetryService"] = {
        {"@odata.id", "/redfish/v1/TelemetryService"}};
#ifdef BMCWEB_ENABLE_REDFISH_LICENSE
    asyncResp->res.jsonValue["LicenseService"] = {
        {"@odata.id", "/redfish/v1/LicenseService"}};
#endif
    asyncResp->res.jsonValue["Cables"] = {{"@odata.id", "/redfish/v1/Cables"}};

    handleServiceRootOem(asyncResp);
}

inline void requestRoutesServiceRoot(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/")
        .privileges(redfish::privileges::getServiceRoot)
        .methods(boost::beast::http::verb::get)(handleServiceRootGet);
}

} // namespace redfish
