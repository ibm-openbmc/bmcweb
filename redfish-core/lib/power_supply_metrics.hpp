#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utility.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/power_supply_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace redfish
{

namespace power_supply_metrics
{

static const char* historyMaximumInterface =
    "org.open_power.Sensor.Aggregation.History.Maximum";
static const char* historyAverageInterface =
    "org.open_power.Sensor.Aggregation.History.Average";

static std::array<const char*, 2> historyInterfaces{historyMaximumInterface,
                                                    historyAverageInterface};

inline void addInputHistoryProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& interface,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    // Get Scale and Values properties
    int64_t scale{0};
    std::vector<std::tuple<uint64_t, int64_t>> values{};
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "Scale", scale,
        "Values", values);

    if (!success)
    {
        BMCWEB_LOG_ERROR << "Unable to unpack input history properties";
        messages::internalError(asyncResp->res);
        return;
    }

    if (values.empty())
    {
        return;
    }

    // Make sure JSON array has correct number of items
    auto& jsonItems =
        asyncResp->res.jsonValue["Oem"]["IBM"]["InputPowerHistoryItems"];
    if (jsonItems.empty())
    {
        // First set of values being added; create array with correct size
        jsonItems = nlohmann::json(values.size(), nlohmann::json::object());
    }
    else if (jsonItems.size() != values.size())
    {
        // Second set of values being added; different size than first set
        messages::internalError(asyncResp->res);
        return;
    }

    // Loop through the values, adding them to the JSON array
    for (unsigned int i = 0; i < values.size(); ++i)
    {
        auto& jsonItem = jsonItems[i];
        const auto [timestamp, value] = values[i];
        jsonItem["Date"] = redfish::time_utils::getDateTimeUintMs(timestamp);
        double scaledValue = static_cast<double>(value) * std::pow(10.0, scale);
        if (interface == historyMaximumInterface)
        {
            jsonItem["Maximum"] = scaledValue;
        }
        else
        {
            jsonItem["Average"] = scaledValue;
        }
    }
}

inline void getInputHistoryServiceAndInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& historyPath,
    std::function<void(const std::string& service,
                       const std::string& interface)>&& callback)
{
    dbus::utility::getDbusObject(
        historyPath, historyInterfaces,
        [asyncResp, callback{std::move(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& object) {
        if (ec || object.empty())
        {
            messages::internalError(asyncResp->res);
            return;
        }

        // Get service that provides the history path
        const std::string& service = object.front().first;

        // Get history interface for history path (Maximum or Average)
        const auto& interfaces = object.front().second;
        auto it = std::find_first_of(interfaces.cbegin(), interfaces.cend(),
                                     historyInterfaces.cbegin(),
                                     historyInterfaces.cend());
        if (it == interfaces.cend())
        {
            messages::internalError(asyncResp->res);
            return;
        }
        const std::string& interface = *it;

        callback(service, interface);
        });
}

inline void getInputHistory(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            std::vector<std::string> historyPaths)
{
    // Get the service and interface for the first history path
    getInputHistoryServiceAndInterface(
        asyncResp, historyPaths.front(),
        [asyncResp, historyPaths](const std::string& service,
                                  const std::string& interface) mutable {
        // Get all properties from the first history path
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, service, historyPaths.front(),
            interface,
            [asyncResp, historyPaths,
             interface](const boost::system::error_code& ec,
                        const dbus::utility::DBusPropertiesMap&
                            propertiesList) mutable {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus response error: " << ec;
                messages::internalError(asyncResp->res);
                return;
            }

            // Add input history properties to the JSON response
            addInputHistoryProperties(asyncResp, interface, propertiesList);

            // Recurse if there are more input history paths
            if (historyPaths.size() > 1)
            {
                historyPaths.erase(historyPaths.cbegin());
                getInputHistory(asyncResp, historyPaths);
            }
            });
        });
}

inline void getValidInputHistoryPaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& powerSupplyId,
    std::function<void(const std::vector<std::string>& historyPaths)>&&
        callback)
{
    // Get chassis D-Bus path
    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp, chassisId, powerSupplyId, callback{std::move(callback)}](
            const std::optional<std::string>& validChassisPath) mutable {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }

        // Get power supply D-Bus path
        redfish::power_supply_utils::getValidPowerSupplyPath(
            asyncResp, *validChassisPath, powerSupplyId,
            [asyncResp, callback{std::move(callback)}](
                const std::string& validPowerSupplyPath) mutable {
            // Get input history D-Bus paths
            redfish::power_supply_utils::getInputHistoryPaths(
                asyncResp, validPowerSupplyPath,
                [asyncResp, callback{std::move(callback)}](
                    const std::vector<std::string>& historyPaths) {
                if (historyPaths.empty())
                {
                    messages::resourceNotFound(asyncResp->res,
                                               "PowerSupplyMetrics", "Metrics");
                    return;
                }

                callback(historyPaths);
                });
            });
        });
}

inline void handleHead(App& app, const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& chassisId,
                       const std::string& powerSupplyId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    getValidInputHistoryPaths(asyncResp, chassisId, powerSupplyId,
                              [asyncResp](const std::vector<std::string>&) {
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/PowerSupplyMetrics/PowerSupplyMetrics.json>; rel=describedby");
    });
}

inline void handleGet(App& app, const crow::Request& req,
                      const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& chassisId,
                      const std::string& powerSupplyId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    getValidInputHistoryPaths(
        asyncResp, chassisId, powerSupplyId,
        [asyncResp, chassisId,
         powerSupplyId](const std::vector<std::string>& historyPaths) {
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/PowerSupplyMetrics/PowerSupplyMetrics.json>; rel=describedby");
        asyncResp->res.jsonValue["@odata.type"] =
            "#PowerSupplyMetrics.v1_0_1.PowerSupplyMetrics";
        asyncResp->res.jsonValue["Name"] = "Metrics for " + powerSupplyId;
        asyncResp->res.jsonValue["Id"] = "Metrics";
        asyncResp->res.jsonValue["@odata.id"] = crow::utility::urlFromPieces(
            "redfish", "v1", "Chassis", chassisId, "PowerSubsystem",
            "PowerSupplies", powerSupplyId, "Metrics");
        asyncResp->res.jsonValue["Oem"]["@odata.type"] =
            "#OemPowerSupplyMetrics.v1_0_0.Oem";
        asyncResp->res.jsonValue["Oem"]["IBM"]["@odata.type"] =
            "#OemPowerSupplyMetrics.v1_0_0.IBM";
        asyncResp->res.jsonValue["Oem"]["IBM"]["InputPowerHistoryItems"] =
            nlohmann::json::array();

        // Get input history values and add them to the response
        getInputHistory(asyncResp, historyPaths);
        });
}

} // namespace power_supply_metrics

inline void requestRoutesPowerSupplyMetrics(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/PowerSubsystem/PowerSupplies/<str>/Metrics")
        .privileges(redfish::privileges::headPowerSupplyMetrics)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(power_supply_metrics::handleHead, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/PowerSubsystem/PowerSupplies/<str>/Metrics")
        .privileges(redfish::privileges::getPowerSupplyMetrics)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(power_supply_metrics::handleGet, std::ref(app)));
}

} // namespace redfish
