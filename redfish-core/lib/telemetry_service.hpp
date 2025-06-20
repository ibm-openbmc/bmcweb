// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/resource.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/telemetry_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/beast/http/verb.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <utility>

namespace redfish
{

inline void handleTelemetryServiceGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#TelemetryService.v1_2_1.TelemetryService";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/TelemetryService";
    asyncResp->res.jsonValue["Id"] = "TelemetryService";
    asyncResp->res.jsonValue["Name"] = "Telemetry Service";

    asyncResp->res.jsonValue["MetricReportDefinitions"]["@odata.id"] =
        "/redfish/v1/TelemetryService/MetricReportDefinitions";
    asyncResp->res.jsonValue["MetricReports"]["@odata.id"] =
        "/redfish/v1/TelemetryService/MetricReports";
    asyncResp->res.jsonValue["MetricDefinitions"]["@odata.id"] =
        "/redfish/v1/TelemetryService/MetricDefinitions";
    asyncResp->res.jsonValue["Triggers"]["@odata.id"] =
        "/redfish/v1/TelemetryService/Triggers";

    dbus::utility::getAllProperties(
        telemetry::service, "/xyz/openbmc_project/Telemetry/Reports",
        "xyz.openbmc_project.Telemetry.ReportManager",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& ret) {
            if (ec == boost::system::errc::host_unreachable)
            {
                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::Absent;
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR("respHandler DBus error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res.jsonValue["Status"]["State"] =
                resource::State::Enabled;

            const size_t* maxReports = nullptr;
            const uint64_t* minInterval = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), ret, "MaxReports", maxReports,
                "MinInterval", minInterval);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (maxReports != nullptr)
            {
                asyncResp->res.jsonValue["MaxReports"] = *maxReports;
            }

            if (minInterval != nullptr)
            {
                asyncResp->res.jsonValue["MinCollectionInterval"] =
                    time_utils::toDurationString(std::chrono::milliseconds(
                        static_cast<time_t>(*minInterval)));
            }
            nlohmann::json::array_t supportedCollectionFunctions;
            supportedCollectionFunctions.emplace_back("Maximum");
            supportedCollectionFunctions.emplace_back("Minimum");
            supportedCollectionFunctions.emplace_back("Average");
            supportedCollectionFunctions.emplace_back("Summation");

            asyncResp->res.jsonValue["SupportedCollectionFunctions"] =
                std::move(supportedCollectionFunctions);
        });
}

inline void requestRoutesTelemetryService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TelemetryService/")
        .privileges(redfish::privileges::getTelemetryService)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleTelemetryServiceGet, std::ref(app)));
}

} // namespace redfish
