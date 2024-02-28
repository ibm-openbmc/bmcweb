#pragma once

#include "app.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "str_utility.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/fan_utils.hpp"

#include <boost/url/format.hpp>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace redfish
{
inline void
    updateFanSensorList(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisId,
                        const std::string& fanSensorPath, double value)
{
    std::string fanSensorName =
        sdbusplus::message::object_path(fanSensorPath).filename();
    if (fanSensorName.empty())
    {
        messages::internalError(asyncResp->res);
        return;
    }

    nlohmann::json::object_t item;
    item["SensorFanArrayExcerpt"]["DataSourceUri"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Sensors/{}", chassisId, fanSensorName);
    item["SensorFanArrayExcerpt"]["DeviceName"] = "Chassis #" + fanSensorName;
    item["SensorFanArrayExcerpt"]["SpeedRPM"] = value;

    nlohmann::json& fanSensorList =
        asyncResp->res.jsonValue["FanSpeedsPercent"];
    fanSensorList.emplace_back(std::move(item));
    std::sort(fanSensorList.begin(), fanSensorList.end(),
              [](const nlohmann::json& c1, const nlohmann::json& c2) {
        return c1["SensorFanArrayExcerpt"]["DataSourceUri"] <
               c2["SensorFanArrayExcerpt"]["DataSourceUri"];
    });
    asyncResp->res.jsonValue["FanSpeedsPercent@odata.count"] =
        fanSensorList.size();
}

inline void
    getFanSensorValues(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& chassisId,
                       const std::vector<std::pair<std::string, std::string>>&
                           serviceAndSensorPaths)
{
    for (const auto& [service, path] : serviceAndSensorPaths)
    {
        sdbusplus::asio::getProperty<double>(
            *crow::connections::systemBus, service, path,
            "xyz.openbmc_project.Sensor.Value", "Value",
            [asyncResp, chassisId, path](const boost::system::error_code& ec,
                                         double value) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "D-Bus response error for getFanSensorValues {}",
                    ec.value());
                messages::internalError(asyncResp->res);
                return;
            }

            updateFanSensorList(asyncResp, chassisId, path, value);
        });
    }
}

inline void
    getFanSpeedsPercent(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& validChassisPath,
                        const std::string& chassisId)
{
    redfish::fan_utils::getFanPaths(
        asyncResp, validChassisPath,
        [asyncResp, chassisId](
            const dbus::utility::MapperGetSubTreePathsResponse& fanPaths) {
        redfish::fan_utils::getFanSensorsObjects(
            asyncResp, fanPaths,
            std::bind_front(getFanSensorValues, asyncResp, chassisId));
    });
}

inline void handleEnvironmentMetricsHead(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    auto respHandler = [asyncResp, chassisId](
                           const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }

        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/EnvironmentMetrics/EnvironmentMetrics.json>; rel=describedby");
    };

    redfish::chassis_utils::getValidChassisPath(asyncResp, chassisId,
                                                std::move(respHandler));
}

inline void getPowerWatts(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Sensor.Value"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/sensors", 0, interfaces,
        [asyncResp,
         chassisId](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreePathsResponse& paths) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR("DBUS response error for getSubTreePaths: {}",
                                 ec.value());
                messages::internalError(asyncResp->res);
            }
            return;
        }
        bool find = false;
        for (const auto& tempPath : paths)
        {
            sdbusplus::message::object_path path(tempPath);
            std::string leaf = path.filename();
            if (leaf == "total_power")
            {
                find = true;
                break;
            }
        }
        if (!find)
        {
            BMCWEB_LOG_DEBUG("There is not total_power");
            return;
        }

        dbus::utility::getDbusObject(
            "/xyz/openbmc_project/sensors/power/total_power", {},
            [asyncResp,
             chassisId](const boost::system::error_code& ec1,
                        const dbus::utility::MapperGetObject& object) {
            if (ec1 || object.empty())
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec1.value());
                messages::internalError(asyncResp->res);
                return;
            }

            sdbusplus::asio::getProperty<double>(
                *crow::connections::systemBus, object.begin()->first,
                "/xyz/openbmc_project/sensors/power/total_power",
                "xyz.openbmc_project.Sensor.Value", "Value",
                [asyncResp, chassisId](const boost::system::error_code& ec2,
                                       double value) {
                if (ec2)
                {
                    if (ec2.value() != EBADR)
                    {
                        BMCWEB_LOG_ERROR("Can't get Power Watts! {}",
                                         ec2.value());
                        messages::internalError(asyncResp->res);
                    }
                    return;
                }
                asyncResp->res.jsonValue["PowerWatts"]["@odata.id"] =
                    boost::urls::format(
                        "/redfish/v1/Chassis/{}/Sensors/total_power",
                        chassisId);
                asyncResp->res.jsonValue["PowerWatts"]["DataSourceUri"] =
                    boost::urls::format(
                        "/redfish/v1/Chassis/{}/Sensors/total_power",
                        chassisId);
                asyncResp->res.jsonValue["PowerWatts"]["Reading"] = value;
            });
        });
    });
}

inline void handleEnvironmentMetricsGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    auto respHandler = [asyncResp, chassisId](
                           const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }

        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/EnvironmentMetrics/EnvironmentMetrics.json>; rel=describedby");
        asyncResp->res.jsonValue["@odata.type"] =
            "#EnvironmentMetrics.v1_3_0.EnvironmentMetrics";
        asyncResp->res.jsonValue["Name"] = "Chassis Environment Metrics";
        asyncResp->res.jsonValue["Id"] = "EnvironmentMetrics";
        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/EnvironmentMetrics", chassisId);

        getFanSpeedsPercent(asyncResp, *validChassisPath, chassisId);
        getPowerWatts(asyncResp, chassisId);
    };

    redfish::chassis_utils::getValidChassisPath(asyncResp, chassisId,
                                                std::move(respHandler));
}

inline void requestRoutesEnvironmentMetrics(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/")
        .privileges(redfish::privileges::headEnvironmentMetrics)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleEnvironmentMetricsHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/")
        .privileges(redfish::privileges::getEnvironmentMetrics)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleEnvironmentMetricsGet, std::ref(app)));
}

} // namespace redfish
