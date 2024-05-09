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

#include "utils/dbus_utils.hpp"

#include <bmcweb_config.h>

#include <app.hpp>
#include <async_resp.hpp>
#include <http_request.hpp>
#include <nlohmann/json.hpp>
#include <persistent_data.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/systemd_utils.hpp>
#include <utils/time_utils.hpp>

namespace redfish
{

inline void
    handleACFWindowActive(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Redfish property ACFWindowActive=true when either of these is true:
    //  - D-Bus property allow_unauth_upload.  (This is aka the Redfish
    //    property AllowUnauthACFUpload which the BMC admin can control.)
    //  - D-Bus property ACFWindowActive.  (This is aka the Redfish
    //    property ACFWindowActive under /redfish/v1/AccountService/
    //    Accounts/service property Oem.IBM.ACF.  The value is provided by
    //    the PanelApp and is true when panel function 74 is active.)
    // Check D-Bus property allow_unauth_upload first.
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/ibmacf/allow_unauth_upload",
        "xyz.openbmc_project.Object.Enable", "Enabled",
        [asyncResp](const boost::system::error_code& ec,
                    const bool allowUnauthACFUpload) {
        if (ec)
        {
            BMCWEB_LOG_ERROR
                << "D-Bus response error reading allow_unauth_upload: " << ec;
            messages::internalError(asyncResp->res);
            return;
        }

        if (allowUnauthACFUpload)
        {
            asyncResp->res.jsonValue["Oem"]["IBM"]["ACFWindowActive"] =
                allowUnauthACFUpload;
            return;
        }

        // Check D-Bus property ACFWindowActive
        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus, "com.ibm.PanelApp",
            "/com/ibm/panel_app", "com.ibm.panel", "ACFWindowActive",
            [asyncResp](const boost::system::error_code& ec1,
                        const bool isACFWindowActive) {
            if (ec1)
            {
                BMCWEB_LOG_ERROR << "Failed to read ACFWindowActive property";
                // Default value when panel app is unreachable.
                asyncResp->res.jsonValue["Oem"]["IBM"]["ACFWindowActive"] =
                    false;
                return;
            }
            asyncResp->res.jsonValue["Oem"]["IBM"]["ACFWindowActive"] =
                isACFWindowActive;
        });
    });
}

inline void
    handleServiceRootOem(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
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
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                connectionNames = object.second;
            if (connectionNames.empty())
            {
                continue;
            }

            for (const auto& connection : connectionNames)
            {
                for (const auto& interfaceName : connection.second)
                {
                    if (interfaceName ==
                        "xyz.openbmc_project.Inventory.Item.System")
                    {
                        sdbusplus::asio::getAllProperties(
                            *crow::connections::systemBus, connection.first,
                            path,
                            "xyz.openbmc_project.Inventory.Decorator.Asset",
                            [asyncResp](const boost::system::error_code ec2,
                                        const dbus::utility::DBusPropertiesMap&
                                            propertiesList) {
                            if (ec2)
                            {
                                // doesn't have to include this
                                // interface
                                return;
                            }
                            BMCWEB_LOG_DEBUG << "Got " << propertiesList.size()
                                             << " properties for system";

                            const std::string* serialNumber = nullptr;
                            const std::string* model = nullptr;

                            const bool success =
                                sdbusplus::unpackPropertiesNoThrow(
                                    dbus_utils::UnpackErrorPrinter(),
                                    propertiesList, "SerialNumber",
                                    serialNumber, "Model", model);

                            if (!success)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            if (serialNumber != nullptr)
                            {
                                asyncResp->res
                                    .jsonValue["Oem"]["IBM"]["SerialNumber"] =
                                    *serialNumber;
                            }

                            if (model != nullptr)
                            {
                                asyncResp->res
                                    .jsonValue["Oem"]["IBM"]["Model"] = *model;
                            }
                        });
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
        redfish::time_utils::getDateTimeOffsetNow();

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
    handleServiceRootHead(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ServiceRoot/ServiceRoot.json>; rel=describedby");
}

inline void handleServiceRootGetImpl(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string uuid = persistent_data::getConfig().systemUuid;
    asyncResp->res.jsonValue["@odata.type"] =
        "#ServiceRoot.v1_12_0.ServiceRoot";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1";
    asyncResp->res.jsonValue["Id"] = "RootService";
    asyncResp->res.jsonValue["Name"] = "Root Service";
    asyncResp->res.jsonValue["RedfishVersion"] = "1.17.0";
    asyncResp->res.jsonValue["Links"]["Sessions"]["@odata.id"] =
        "/redfish/v1/SessionService/Sessions";
    asyncResp->res.jsonValue["AccountService"]["@odata.id"] =
        "/redfish/v1/AccountService";
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
    asyncResp->res.jsonValue["AggregationService"]["@odata.id"] =
        "/redfish/v1/AggregationService";
#endif
    asyncResp->res.jsonValue["Chassis"]["@odata.id"] = "/redfish/v1/Chassis";
    asyncResp->res.jsonValue["JsonSchemas"]["@odata.id"] =
        "/redfish/v1/JsonSchemas";
    asyncResp->res.jsonValue["Managers"]["@odata.id"] = "/redfish/v1/Managers";
    asyncResp->res.jsonValue["SessionService"]["@odata.id"] =
        "/redfish/v1/SessionService";
    asyncResp->res.jsonValue["Systems"]["@odata.id"] = "/redfish/v1/Systems";
    asyncResp->res.jsonValue["Registries"]["@odata.id"] =
        "/redfish/v1/Registries";
    asyncResp->res.jsonValue["UpdateService"]["@odata.id"] =
        "/redfish/v1/UpdateService";
    asyncResp->res.jsonValue["UUID"] = uuid;
    asyncResp->res.jsonValue["CertificateService"]["@odata.id"] =
        "/redfish/v1/CertificateService";
    asyncResp->res.jsonValue["Tasks"]["@odata.id"] = "/redfish/v1/TaskService";
    asyncResp->res.jsonValue["EventService"]["@odata.id"] =
        "/redfish/v1/EventService";
    asyncResp->res.jsonValue["TelemetryService"]["@odata.id"] =
        "/redfish/v1/TelemetryService";
    asyncResp->res.jsonValue["Cables"]["@odata.id"] = "/redfish/v1/Cables";

    nlohmann::json& protocolFeatures =
        asyncResp->res.jsonValue["ProtocolFeaturesSupported"];
    protocolFeatures["ExcerptQuery"] = false;

    protocolFeatures["ExpandQuery"]["ExpandAll"] =
        bmcwebInsecureEnableQueryParams;
    // This is the maximum level is defined by us
    if (bmcwebInsecureEnableQueryParams)
    {
        protocolFeatures["ExpandQuery"]["MaxLevels"] = 3;
    }
    protocolFeatures["ExpandQuery"]["Levels"] = bmcwebInsecureEnableQueryParams;
    protocolFeatures["ExpandQuery"]["Links"] = bmcwebInsecureEnableQueryParams;
    protocolFeatures["ExpandQuery"]["NoLinks"] =
        bmcwebInsecureEnableQueryParams;
    protocolFeatures["FilterQuery"] = false;
    protocolFeatures["OnlyMemberQuery"] = true;
    protocolFeatures["SelectQuery"] = true;
    protocolFeatures["DeepOperations"]["DeepPOST"] = false;
    protocolFeatures["DeepOperations"]["DeepPATCH"] = false;

#ifdef BMCWEB_ENABLE_REDFISH_LICENSE
    asyncResp->res.jsonValue["LicenseService"] = {
        {"@odata.id", "/redfish/v1/LicenseService"}};
#endif
}
inline void
    handleServiceRootGet(App& app, const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    handleServiceRootHead(app, req, asyncResp);
    handleServiceRootGetImpl(asyncResp);
    handleServiceRootOem(asyncResp);
}

inline void requestRoutesServiceRoot(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/")
        .privileges(redfish::privileges::headServiceRoot)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleServiceRootHead, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/")
        .privileges(redfish::privileges::getServiceRoot)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleServiceRootGet, std::ref(app)));
}

} // namespace redfish
