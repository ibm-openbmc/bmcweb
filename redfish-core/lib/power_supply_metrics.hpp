#pragma once

namespace redfish
{

/**
 * @brief Get power supply average and date values given chassis and power
 * supply IDs.
 *
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisID - Chassis to which the values are associated.
 * @param[in] powerSupplyID - Power supply to which the values are associated.
 *
 * @return None.
 */
inline void getAverageValues(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& chassisID,
                             const std::string& powerSupplyID)
{
    BMCWEB_LOG_INFO << "Get power supply average input power values";
    BMCWEB_LOG_DEBUG << "ChassisID: " << chassisID;
    BMCWEB_LOG_DEBUG << "PowerSupplyID: " << powerSupplyID;
    // Setup date/timestamp and average values as arrays.
    aResp->res.jsonValue["Date"] = nlohmann::json::array();
    aResp->res.jsonValue["Average"] = nlohmann::json::array();

    auto averagePath = "/org/open_power/sensors/aggregation/per_30s/" +
                       powerSupplyID + "_input_power/average";

    crow::connections::systemBus->async_method_call(
        [aResp, powerSupplyID, averagePath](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& [serviceName, interfaceList] : object)
            {
                for (const auto& interface : interfaceList)
                {
                    if (interface ==
                        "org.open_power.Sensor.Aggregation.History.Average")
                    {
                        crow::connections::systemBus->async_method_call(
                            [aResp, powerSupplyID](
                                const boost::system::error_code ec2,
                                const std::variant<std::vector<
                                    std::tuple<std::uint64_t, std::int64_t>>>&
                                    values_rsp) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_DEBUG << "DBUS response error";
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                const std::vector<
                                    std::tuple<std::uint64_t, std::int64_t>>*
                                    values_ptr =
                                        std::get_if<std::vector<std::tuple<
                                            std::uint64_t, std::int64_t>>>(
                                            &values_rsp);
                                if (values_ptr != nullptr)
                                {
                                    nlohmann::json& averages =
                                        aResp->res.jsonValue["Average"];
                                    nlohmann::json& dates =
                                        aResp->res.jsonValue["Date"];

                                    for (const auto& values : *values_ptr)
                                    {
                                        // The first value returned is the
                                        // timestamp, it is in milliseconds
                                        // since the Epoch. Divide that by 1000,
                                        // to get date/time via seconds since
                                        // the Epoch.
                                        dates.push_back(
                                            crow::utility::getDateTime(
                                                static_cast<std::time_t>(
                                                    (std::get<0>(values) /
                                                     1000))));
                                        // The second value returned is in
                                        // watts. It is the average watts this
                                        // power supply has used in a 30 second
                                        // interval.
                                        averages.push_back(std::get<1>(values));
                                    }
                                }
                                else
                                {
                                    BMCWEB_LOG_ERROR
                                        << "Failed to find power supply input "
                                           "history Values data for:"
                                        << powerSupplyID;
                                }
                            },
                            serviceName, averagePath,
                            "org.freedesktop.DBus.Properties", "Get", interface,
                            "Values");
                    }
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", averagePath,
        std::array<const char*, 1>{
            "org.open_power.Sensor.Aggregation.History.Average"});
}

/**
 * @brief Get power supply maximum and date values given chassis and power
 * supply IDs.
 *
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisID - Chassis to which the values are associated.
 * @param[in] powerSupplyID - Power supply to which the values are associated.
 *
 * @return None.
 */
inline void getMaxValues(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& chassisID,
                         const std::string& powerSupplyID)
{
    BMCWEB_LOG_INFO << "Get power supply maximum input power values";
    BMCWEB_LOG_DEBUG << "ChassisID: " << chassisID;
    BMCWEB_LOG_DEBUG << "PowerSupplyID: " << powerSupplyID;

    // Setup Max as an array.
    aResp->res.jsonValue["Max"] = nlohmann::json::array();

    auto maximumPath = "/org/open_power/sensors/aggregation/per_30s/" +
                       powerSupplyID + "_input_power/maximum";

    crow::connections::systemBus->async_method_call(
        [aResp, powerSupplyID, maximumPath](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& [serviceName, interfaceList] : object)
            {
                for (const auto& interface : interfaceList)
                {
                    if (interface ==
                        "org.open_power.Sensor.Aggregation.History.Maximum")
                    {
                        crow::connections::systemBus->async_method_call(
                            [aResp, powerSupplyID](
                                const boost::system::error_code ec2,
                                const std::variant<std::vector<
                                    std::tuple<std::uint64_t, std::int64_t>>>&
                                    values_rsp) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_DEBUG << "DBUS response error";
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                const std::vector<
                                    std::tuple<std::uint64_t, std::int64_t>>*
                                    values_ptr =
                                        std::get_if<std::vector<std::tuple<
                                            std::uint64_t, std::int64_t>>>(
                                            &values_rsp);
                                if (values_ptr != nullptr)
                                {
                                    nlohmann::json& maximums =
                                        aResp->res.jsonValue["Max"];
                                    for (const auto& values : *values_ptr)
                                    {
                                        // The first value returned is the
                                        // timestamp, already handled in
                                        // getAverageValues().
                                        // The second value returned is in
                                        // watts. It is the maximum watts this
                                        // power supply has used in a 30 second
                                        // interval.
                                        maximums.push_back(std::get<1>(values));
                                    }
                                }
                                else
                                {
                                    BMCWEB_LOG_ERROR
                                        << "Failed to find power supply input "
                                           "history Values data for:"
                                        << powerSupplyID;
                                }
                            },
                            serviceName, maximumPath,
                            "org.freedesktop.DBus.Properties", "Get", interface,
                            "Values");
                    }
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", maximumPath,
        std::array<const char*, 1>{
            "org.open_power.Sensor.Aggregation.History.Maximum"});
}

/**
 * Systems derived class for delivering OemPowerSupplyMetric Schema.
 */
inline void requestRoutesPowerSupplyMetrics(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PowerSubsystem/"
                      "PowerSupplies/<str>/Metrics")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& param, const std::string& param2) {
                const std::string& chassisId = param;
                const std::string& powerSupplyId = param2;

                BMCWEB_LOG_INFO << "ChassisID: " << chassisId;
                BMCWEB_LOG_INFO << "PowerSupplyID: " << powerSupplyId;

                asyncResp->res.jsonValue["@odata.type"] =
                    "#OemPowerSupplyMetric.v1_0_0";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Chassis/" + chassisId +
                    "/PowerSubSystem/PowerSupplies/" + powerSupplyId +
                    "/Metrics";
                asyncResp->res.jsonValue["Name"] =
                    "Metrics for " + powerSupplyId;
                asyncResp->res.jsonValue["Id"] = "Metrics";

                getAverageValues(asyncResp, chassisId, powerSupplyId);
                getMaxValues(asyncResp, chassisId, powerSupplyId);
            });
}
} // namespace redfish
