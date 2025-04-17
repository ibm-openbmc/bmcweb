// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2018 Intel Corporation
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "persistent_data.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/dbus_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/time_utils.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{

inline void afterHandleACFWindowActive(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const bool allowUnauthACFUpload)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR(
                "D-Bus response error reading allow_unauth_upload: {}",
                ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    if (allowUnauthACFUpload)
    {
        asyncResp->res.jsonValue["Oem"]["IBM"]["ACFWindowActive"] =
            allowUnauthACFUpload;
        return;
    }

    // Check D-Bus property ACFWindowActive
    dbus::utility::getProperty<bool>(
        "com.ibm.PanelApp", "/com/ibm/panel_app", "com.ibm.panel",
        "ACFWindowActive",
        [asyncResp](const boost::system::error_code& ec1,
                    const bool isACFWindowActive) {
            if (ec1)
            {
                BMCWEB_LOG_ERROR("Failed to read ACFWindowActive property");
                // Default value when panel app is unreachable.
                asyncResp->res.jsonValue["Oem"]["IBM"]["ACFWindowActive"] =
                    false;
                return;
            }
            asyncResp->res.jsonValue["Oem"]["IBM"]["ACFWindowActive"] =
                isACFWindowActive;
        });
}

inline void handleACFWindowActive(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Redfish property ACFWindowActive=true when either of these is true:
    //  - D-Bus property allow_unauth_upload.  (This is aka the Redfish
    //    property AllowUnauthACFUpload which the BMC admin can control.)
    //  - D-Bus property ACFWindowActive.  (This is aka the Redfish
    //    property ACFWindowActive under /redfish/v1/AccountService/
    //    Accounts/service property Oem.IBM.ACF.  The value is provided by
    //    the PanelApp and is true when panel function 74 is active.)
    // Check D-Bus property allow_unauth_upload first.
    dbus::utility::getProperty<bool>(
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/ibmacf/allow_unauth_upload",
        "xyz.openbmc_project.Object.Enable", "Enabled",
        std::bind_front(afterHandleACFWindowActive, asyncResp));
}

inline void fillServiceRootOemProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    if (ec)
    {
        // doesn't have to include this
        // interface
        return;
    }
    BMCWEB_LOG_DEBUG("Got {} properties for system", propertiesList.size());

    const std::string* serialNumber = nullptr;
    const std::string* model = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "SerialNumber",
        serialNumber, "Model", model);

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    if (serialNumber != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["IBM"]["SerialNumber"] = *serialNumber;
    }

    if (model != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["IBM"]["Model"] = *model;
    }
}

inline void afterHandleServiceRootOem(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }
    // Iterate over all retrieved ObjectPaths.
    for (const auto& object : subtree)
    {
        const std::string& path = object.first;
        BMCWEB_LOG_DEBUG("Got path: {}", path);
        const std::vector<std::pair<std::string, std::vector<std::string>>>&
            connectionNames = object.second;
        if (connectionNames.empty())
        {
            continue;
        }

        for (const auto& connection : connectionNames)
        {
            auto iter = std::ranges::find(
                connection.second, "xyz.openbmc_project.Inventory.Item.System");
            if (iter == connection.second.end())
            {
                continue;
            }

            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, connection.first, path,
                "xyz.openbmc_project.Inventory.Decorator.Asset",
                [asyncResp](
                    const boost::system::error_code& ec2,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
                    fillServiceRootOemProperties(asyncResp, ec2,
                                                 propertiesList);
                });
        }
    }
}

inline void handleServiceRootOem(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Decorator.Asset"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(afterHandleServiceRootOem, asyncResp));

    std::pair<std::string, std::string> redfishDateTimeOffset =
        redfish::time_utils::getDateTimeOffsetNow();

    asyncResp->res.jsonValue["Oem"]["IBM"]["DateTime"] =
        redfishDateTimeOffset.first;
    asyncResp->res.jsonValue["Oem"]["IBM"]["DateTimeLocalOffset"] =
        redfishDateTimeOffset.second;
    asyncResp->res.jsonValue["Oem"]["IBM"]["@odata.type"] =
        "#IBMServiceRoot.v1_0_0.IBM";

    handleACFWindowActive(asyncResp);
}

inline void handleServiceRootHead(
    App& app, const crow::Request& req,
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
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ServiceRoot/ServiceRoot.json>; rel=describedby");

    std::string uuid = persistent_data::getConfig().systemUuid;
    asyncResp->res.jsonValue["@odata.type"] =
        "#ServiceRoot.v1_15_0.ServiceRoot";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1";
    asyncResp->res.jsonValue["Id"] = "RootService";
    asyncResp->res.jsonValue["Name"] = "Root Service";
    asyncResp->res.jsonValue["RedfishVersion"] = "1.17.0";
    asyncResp->res.jsonValue["Links"]["Sessions"]["@odata.id"] =
        "/redfish/v1/SessionService/Sessions";
    asyncResp->res.jsonValue["AccountService"]["@odata.id"] =
        "/redfish/v1/AccountService";
    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        asyncResp->res.jsonValue["AggregationService"]["@odata.id"] =
            "/redfish/v1/AggregationService";
    }
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

    asyncResp->res.jsonValue["Links"]["ManagerProvidingService"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Managers/{}",
                            BMCWEB_REDFISH_MANAGER_URI_NAME);

    nlohmann::json& protocolFeatures =
        asyncResp->res.jsonValue["ProtocolFeaturesSupported"];
    protocolFeatures["ExcerptQuery"] = false;

    protocolFeatures["ExpandQuery"]["ExpandAll"] =
        BMCWEB_INSECURE_ENABLE_REDFISH_QUERY;
    // This is the maximum level is defined by us
    if constexpr (BMCWEB_INSECURE_ENABLE_REDFISH_QUERY)
    {
        protocolFeatures["ExpandQuery"]["MaxLevels"] = 3;
    }
    protocolFeatures["ExpandQuery"]["Levels"] =
        BMCWEB_INSECURE_ENABLE_REDFISH_QUERY;
    protocolFeatures["ExpandQuery"]["Links"] =
        BMCWEB_INSECURE_ENABLE_REDFISH_QUERY;
    protocolFeatures["ExpandQuery"]["NoLinks"] =
        BMCWEB_INSECURE_ENABLE_REDFISH_QUERY;
    protocolFeatures["FilterQuery"] = BMCWEB_INSECURE_ENABLE_REDFISH_QUERY;
    protocolFeatures["OnlyMemberQuery"] = true;
    protocolFeatures["SelectQuery"] = true;
    protocolFeatures["DeepOperations"]["DeepPOST"] = false;
    protocolFeatures["DeepOperations"]["DeepPATCH"] = false;
}
inline void handleServiceRootGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

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
