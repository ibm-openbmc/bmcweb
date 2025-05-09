// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/control.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/fan_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/sensor_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{
inline void updateFanSensorList(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const std::string& chassisId,
    const std::string& fanSensorPath, double value)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    sdbusplus::message::object_path sensorPath(fanSensorPath);
    std::string fanSensorName = sensorPath.filename();
    std::string fanSensorType = sensorPath.parent_path().filename();
    if (fanSensorName.empty() || fanSensorType.empty())
    {
        BMCWEB_LOG_ERROR("Fan Sensor name is empty and invalid");
        messages::internalError(asyncResp->res);
        return;
    }

    std::string fanSensorId =
        sensor_utils::getSensorId(fanSensorName, fanSensorType);

    nlohmann::json::object_t item;
    item["DataSourceUri"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Sensors/{}", chassisId, fanSensorId);
    item["DeviceName"] = "Chassis #" + fanSensorName;
    item["SpeedRPM"] = value;

    nlohmann::json& fanSensorList =
        asyncResp->res.jsonValue["FanSpeedsPercent"];
    fanSensorList.emplace_back(std::move(item));
    asyncResp->res.jsonValue["FanSpeedsPercent@odata.count"] =
        fanSensorList.size();

    nlohmann::json::array_t* fanSensorArray =
        fanSensorList.get_ptr<nlohmann::json::array_t*>();
    if (fanSensorArray == nullptr)
    {
        BMCWEB_LOG_ERROR("Missing FanSpeedsPercent Json array");
        messages::internalError(asyncResp->res);
        return;
    }

    json_util::sortJsonArrayByKey(*fanSensorArray, "DataSourceUri");
}

inline void getFanSensorsValue(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    std::vector<std::pair<std::string, std::string>>& sensorsPathAndService)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    for (const auto& [service, sensorPath] : sensorsPathAndService)
    {
        dbus::utility::getProperty<double>(
            service, sensorPath, "xyz.openbmc_project.Sensor.Value", "Value",
            [asyncResp, chassisId, sensorPath](
                const boost::system::error_code& ec1, const double value) {
                updateFanSensorList(asyncResp, ec1, chassisId, sensorPath,
                                    value);
            });
    }
}

inline void afterGetFanSpeedsPercent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& fanPaths)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }
    for (const std::string& fanPath : fanPaths)
    {
        fan_utils::getFanSensorObjects(
            fanPath, std::bind_front(getFanSensorsValue, asyncResp, chassisId));
    }
}

inline void getFanSpeedsPercent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& validChassisPath, const std::string& chassisId)
{
    fan_utils::getFanPaths(
        validChassisPath,
        std::bind_front(afterGetFanSpeedsPercent, asyncResp, chassisId));
}

inline void afterGetPowerWatts(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& path,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& valuesDict)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error for PowerWatts {}", ec);
            messages::internalError(asyncResp->res);
        }
        return;
    }

    nlohmann::json item = nlohmann::json::object();

    /* Don't return an error for a failure to fill in properties from the
     * single sensor. Just skip adding it.
     */
    if (sensor_utils::objectExcerptToJson(
            path, chassisId,
            sensor_utils::ChassisSubNode::environmentMetricsNode, "power",
            valuesDict, item))
    {
        asyncResp->res.jsonValue["PowerWatts"] = std::move(item);
    }
}

inline void handleTotalPowerSensor(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& sensorPath,
    const std::string& serviceName, const boost::system::error_code& ec,
    const std::vector<std::string>& purposeList)
{
    BMCWEB_LOG_DEBUG("handleTotalPowerSensor: {}", sensorPath);
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("D-Bus response error for {} Sensor.Purpose: {}",
                             sensorPath, ec);
            messages::internalError(asyncResp->res);
        }
        return;
    }

    for (const std::string& purposeStr : purposeList)
    {
        if (purposeStr ==
            "xyz.openbmc_project.Sensor.Purpose.SensorPurpose.TotalPower")
        {
            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, serviceName, sensorPath,
                "xyz.openbmc_project.Sensor.Value",
                [asyncResp, chassisId, sensorPath](
                    const boost::system::error_code& ec1,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
                    afterGetPowerWatts(asyncResp, chassisId, sensorPath, ec1,
                                       propertiesList);
                });
            return;
        }
    }
}

inline void getTotalPowerSensor(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const sensor_utils::SensorServicePathList& sensorsServiceAndPath)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            messages::internalError(asyncResp->res);
        }
        return;
    }

    for (const auto& [serviceName, sensorPath] : sensorsServiceAndPath)
    {
        dbus::utility::getProperty<std::vector<std::string>>(
            serviceName, sensorPath, "xyz.openbmc_project.Sensor.Purpose",
            "Purpose",
            std::bind_front(handleTotalPowerSensor, asyncResp, chassisId,
                            sensorPath, serviceName));
    }
}

inline void getPowerWatts(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& validChassisPath,
                          const std::string& chassisId)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Sensor.Purpose"};
    sensor_utils::getAllSensorObjects(
        validChassisPath, "/xyz/openbmc_project/sensors/power", interfaces, 1,
        std::bind_front(getTotalPowerSensor, asyncResp, chassisId));
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

inline void setPowerSetPoint(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, uint32_t powerCap)
{
    BMCWEB_LOG_DEBUG("Set Power Limit Watts Set Point");

    std::string path = "/xyz/openbmc_project/control/host0/power_cap";
    setDbusProperty(asyncResp, "SetPoint", "xyz.openbmc_project.Settings", path,
                    "xyz.openbmc_project.Control.Power.Cap", "PowerCap",
                    powerCap);
}

inline void setPowerControlMode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& controlMode)
{
    BMCWEB_LOG_DEBUG("Set Power Limit Watts Control Mode");
    bool powerCapEnable = false;
    if (controlMode == "Disabled")
    {
        powerCapEnable = false;
    }
    else if (controlMode == "Automatic")
    {
        powerCapEnable = true;
    }
    else
    {
        BMCWEB_LOG_WARNING("Power Control Mode  does not support this mode: {}",
                           controlMode);
        messages::propertyValueNotInList(asyncResp->res, controlMode,
                                         "ControlMode");
        return;
    }

    std::string path = "/xyz/openbmc_project/control/host0/power_cap";
    setDbusProperty(asyncResp, "ControlMode", "xyz.openbmc_project.Settings",
                    path, "xyz.openbmc_project.Control.Power.Cap",
                    "PowerCapEnable", powerCapEnable);
}

inline void handlePowerLimitWattsControl(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error: {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    const uint32_t* minCap = nullptr;
    const uint32_t* maxCap = nullptr;
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "MinPowerCapValue",
        minCap, "MaxPowerCapValue", maxCap);
    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    if (minCap != nullptr)
    {
        asyncResp->res.jsonValue["PowerLimitWatts"]["AllowableMin"] = *minCap;
    }
    if (maxCap != nullptr)
    {
        asyncResp->res.jsonValue["PowerLimitWatts"]["AllowableMax"] = *maxCap;
    }
}

inline void handlePowerCap(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error: {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    asyncResp->res.jsonValue["PowerLimitWatts"]["SetPoint"] = 0;
    asyncResp->res.jsonValue["PowerLimitWatts"]["ControlMode"] =
        control::ControlMode::Automatic;

    const uint32_t* powerCap = nullptr;
    const bool* powerCapEnable = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "PowerCap", powerCap,
        "PowerCapEnable", powerCapEnable);

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    if (powerCap != nullptr)
    {
        asyncResp->res.jsonValue["PowerLimitWatts"]["SetPoint"] = *powerCap;
    }

    if (powerCapEnable != nullptr && !*powerCapEnable)
    {
        asyncResp->res.jsonValue["PowerLimitWatts"]["ControlMode"] =
            control::ControlMode::Disabled;
    }
}

inline void getPowerLimitWatts(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getAllProperties(
        *crow::connections::systemBus, "org.open_power.OCC.Control",
        "/xyz/openbmc_project/control/host0/power_cap_limits",
        "xyz.openbmc_project.Control.Power.CapLimits",
        std::bind_front(handlePowerLimitWattsControl, asyncResp));

    dbus::utility::getAllProperties(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/control/host0/power_cap",
        "xyz.openbmc_project.Control.Power.Cap",
        std::bind_front(handlePowerCap, asyncResp));
}

inline void doEnvironmentMetricsGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const std::optional<std::string>& validChassisPath)
{
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

    getPowerWatts(asyncResp, *validChassisPath, chassisId);
    getPowerLimitWatts(asyncResp);
    getFanSpeedsPercent(asyncResp, *validChassisPath, chassisId);
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

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doEnvironmentMetricsGet, asyncResp, chassisId));
}

inline void handleEnvironmentMetricsPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<uint32_t> setPoint;
    std::optional<std::string> controlMode;
    if (!json_util::readJsonPatch(req, asyncResp->res,
                                  "PowerLimitWatts/SetPoint", setPoint,
                                  "PowerLimitWatts/ControlMode", controlMode))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp, chassisId, setPoint,
         controlMode](const std::optional<std::string>& validChassisPath) {
            if (!validChassisPath)
            {
                BMCWEB_LOG_WARNING("Chassis {} not found", chassisId);
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }

            if (setPoint)
            {
                setPowerSetPoint(asyncResp, *setPoint);
            }

            if (controlMode)
            {
                setPowerControlMode(asyncResp, *controlMode);
            }
        });
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

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/")
        .privileges(redfish::privileges::patchEnvironmentMetrics)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleEnvironmentMetricsPatch, std::ref(app)));
}

} // namespace redfish
