#pragma once

namespace redfish
{

using averageMaxEntry = std::tuple<std::uint64_t, std::int64_t>;
using historyEntry = std::tuple<std::uint64_t, std::int64_t, std::int64_t>;
using averageMaxArray = std::vector<averageMaxEntry>;
using historyArray = std::vector<historyEntry>;
const std::string averageInterface =
    "org.open_power.Sensor.Aggregation.History.Average";
const std::string maximumInterface =
    "org.open_power.Sensor.Aggregation.History.Maximum";

/**
 * @brief Parse date/time, average, and maximum values into response.
 *
 * @param[in/out] aResp - Shared pointer for asynchronous calls.
 * @param[in] averageValues - populated array of date/time and average values.
 * @param[in] maximumValues - populated array of date/time and maximum values.
 *
 * @return None.
 */
inline void parseAverageMaximum(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const averageMaxArray& averageValues,
                                const averageMaxArray& maximumValues)
{
    // Take date/time and average from averageValues and maximum from
    // maximumValues to populate each inputHistoryItem entry.
    nlohmann::json& jsonResponse = aResp->res.jsonValue;
    nlohmann::json& inputPowerHistoryItemArray =
        jsonResponse["Oem"]["IBM"]["InputPowerHistoryItems"];
    inputPowerHistoryItemArray = nlohmann::json::array();

    std::vector<averageMaxEntry>::const_iterator average =
        averageValues.begin();
    std::vector<averageMaxEntry>::const_iterator maximum =
        maximumValues.begin();
    for (; average != averageValues.end() && maximum != maximumValues.end();
         average++, maximum++)
    {

        // The first value returned is the timestamp, it is in milliseconds
        // since the Epoch. Divide that by 1000, to get date/time via seconds
        // since the Epoch (string).
        // Second value from average and maximum is just an integer type value
        // representing watts.
        inputPowerHistoryItemArray.push_back(
            {{"Date",
              crow::utility::getDateTimeUint((std::get<0>(*average) / 1000))},
             {"Average", std::get<1>(*average)},
             {"Maximum", std::get<1>(*maximum)}});
    }
}

/**
 * @brief Gets the values from the Maximum interface and populates array.
 *
 * After getting maximum values, proceed to populating Redfish JSON response
 * properties.
 *
 * @param[in/out] aResp - Shared pointer for asynchronous calls.
 * @param[in] serviceName - The service providing the Maximum interface.
 * @param[in] maximumPath - The object path the Maximum interface is on.
 * @param[in] averageValues - Populated vector of date/time and average values.
 *
 * @return None.
 */
inline void getMaximumValues(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& serviceName,
                             const std::string& maximumPath,
                             const averageMaxArray& averageValues)
{
    BMCWEB_LOG_DEBUG << "Get Values from serviceName: " << serviceName
                     << " objectPath: " << maximumPath
                     << " interfaceName: " << maximumInterface;

    crow::connections::systemBus->async_method_call(
        [aResp, averageValues](
            const boost::system::error_code ec2,
            const std::variant<averageMaxArray>& intfValues) mutable {
            if (ec2)
            {
                BMCWEB_LOG_DEBUG << "D-Bus response error";
                messages::internalError(aResp->res);
                return;
            }
            const averageMaxArray* intfValuesPtr =
                std::get_if<averageMaxArray>(&intfValues);
            if (intfValuesPtr == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }

            averageMaxArray maximumValues;

            for (const auto& values : *intfValuesPtr)
            {
                // The first value returned is the timestamp, it is in
                // milliseconds since the Epoch.
                auto dateTime = std::get<0>(values);
                // The second value returned is in watts. The maximum watts this
                // power supply has used in a 30 second interval.
                auto value = std::get<1>(values);

                BMCWEB_LOG_DEBUG
                    << "Date/Time: "
                    << crow::utility::getDateTimeUint(dateTime / 1000);
                BMCWEB_LOG_DEBUG << "Maximum Value: " << value;

                maximumValues.emplace_back(dateTime, value);
            }

            parseAverageMaximum(aResp, averageValues, maximumValues);
        },
        serviceName, maximumPath, "org.freedesktop.DBus.Properties", "Get",
        maximumInterface, "Values");
}

/**
 * @brief Gets the values from the Average interface and populates array.
 *
 * After getting average values, proceed to get maximum values.
 *
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] serviceName - The serviceName providing the average/maximum values
 *            interfaces.
 * @param[in] averagePath - Object path to the Average Values interface.
 * @param[in] maximumPath - Object path to the Maximum Values interface.
 *
 * @return None.
 */
inline void
    getAverageMaximumValues(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& serviceName,
                            const std::string& averagePath,
                            const std::string& maximumPath)
{
    BMCWEB_LOG_DEBUG << "Get Values from serviceName: " << serviceName
                     << " objectPath: " << averagePath
                     << " interfaceName: " << averageInterface;

    crow::connections::systemBus->async_method_call(
        [aResp, serviceName,
         maximumPath](const boost::system::error_code ec2,
                      const std::variant<averageMaxArray>& intfValues) mutable {
            if (ec2)
            {
                BMCWEB_LOG_DEBUG << "D-Bus response error";
                messages::internalError(aResp->res);
                return;
            }

            const averageMaxArray* intfValuesPtr =
                std::get_if<averageMaxArray>(&intfValues);
            if (intfValuesPtr == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }

            // Populate below with data read from D-Bus.
            averageMaxArray averageValues;
            for (const auto& values : *intfValuesPtr)
            {
                // The first value returned is the timestamp, it is in
                // milliseconds since the Epoch.
                auto dateTime = std::get<0>(values);
                // The second value returned is in watts. It is the average
                // watts this power supply has used in a 30 second interval.
                auto value = std::get<1>(values);

                BMCWEB_LOG_DEBUG
                    << "DateTime: "
                    << crow::utility::getDateTimeUint(dateTime / 1000);
                BMCWEB_LOG_DEBUG << "Values: " << value;

                averageValues.emplace_back(dateTime, value);
            }

            getMaximumValues(aResp, serviceName, maximumPath, averageValues);
        },
        serviceName, averagePath, "org.freedesktop.DBus.Properties", "Get",
        averageInterface, "Values");
}

/**
 * @brief Get power supply average, maximum and date values given chassis and
 * power supply IDs.
 *
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisID - Chassis to which the values are associated.
 * @param[in] powerSupplyID - Power supply to which the values are associated.
 *
 * @return None.
 */
inline void getValues(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                      const std::string& chassisID,
                      const std::string& powerSupplyID)
{
    BMCWEB_LOG_DEBUG
        << "Get power supply date/average/maximum input power values";
    // Setup InputPowerHistoryItems values array.
    // It will have 0 to many date/timestamp, average, and maximum entries.
    aResp->res
        .jsonValue["Oem"]["IBM"]["InputPowerHistoryItems"]["@odata.type"] =
        "#OemPowerSupplyMetric.InputPowerHistoryItems";

    const std::array<const char*, 2> interfaces = {
        "org.open_power.Sensor.Aggregation.History.Average",
        "org.open_power.Sensor.Aggregation.History.Maximum"};

    crow::connections::systemBus->async_method_call(
        [aResp, chassisID, powerSupplyID](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                intfSubTree) mutable {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "D-Bus response error on GetSubTree " << ec;
                messages::internalError(aResp->res);
                return;
            }

            std::string serviceName;
            std::string averagePath;
            std::string maximumPath;

            for (const auto& [objectPath, connectionNames] : intfSubTree)
            {
                if (objectPath.empty())
                {
                    BMCWEB_LOG_DEBUG << "Error getting D-Bus object!";
                    messages::internalError(aResp->res);
                    return;
                }

                const std::string& validPath = objectPath;
                auto psuMatchStr = powerSupplyID + "_input_power";
                std::string psuInputPowerStr;
                // validPath -> {psu}_input_power
                // 5 below comes from
                // /org/open_power/sensors/aggregation/per_30s/
                //   0      1         2         3         4
                // .../{psu}_input_power/[average,maximum]
                //           5..............6
                if (!dbus::utility::getNthStringFromPath(validPath, 5,
                                                         psuInputPowerStr))
                {
                    BMCWEB_LOG_ERROR << "Got invalid path " << validPath;
                    messages::invalidObject(aResp->res, validPath);
                    return;
                }

                if (psuInputPowerStr != psuMatchStr)
                {
                    // not this power supply, continue to next objectPath...
                    continue;
                }

                BMCWEB_LOG_DEBUG << "Got validPath: " << validPath;
                for (const auto& connection : connectionNames)
                {
                    serviceName = connection.first;

                    for (const auto& interfaceName : connection.second)
                    {
                        if (interfaceName == "org.open_power.Sensor."
                                             "Aggregation.History.Average")
                        {
                            averagePath = validPath;
                        }
                        else if (interfaceName == "org.open_power.Sensor."
                                                  "Aggregation.History.Maximum")
                        {
                            maximumPath = validPath;
                        }
                    }
                }
            }

            getAverageMaximumValues(aResp, serviceName, averagePath,
                                    maximumPath);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/org/open_power/sensors/aggregation/per_30s", 0, interfaces);
}

/**
 * Systems derived class for delivering OemPowerSupplyMetrics Schema.
 */
inline void requestRoutesPowerSupplyMetrics(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PowerSubsystem/"
                      "PowerSupplies/<str>/Metrics")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisID, const std::string& powerSupplyID) {
                auto getChassisID = [asyncResp, chassisID, powerSupplyID](
                                        const std::optional<std::string>&
                                            validChassisID) {
                    if (!validChassisID)
                    {
                        BMCWEB_LOG_ERROR << "Not a valid chassis ID:"
                                         << chassisID;
                        messages::resourceNotFound(asyncResp->res, "Chassis",
                                                   chassisID);
                        return;
                    }

                    if (chassisID != "chassis")
                    {
                        BMCWEB_LOG_ERROR << "No Metrics for chassis ID:"
                                         << chassisID;
                        messages::resourceNotFound(asyncResp->res, "Chassis",
                                                   chassisID);
                        return;
                    }

                    BMCWEB_LOG_DEBUG << "ChassisID: " << chassisID;

                    auto getPowerSupplyHandler =
                        [asyncResp, chassisID,
                         powerSupplyID](const std::optional<std::string>&
                                            validPowerSupplyPath,
                                        [[maybe_unused]] const std::string&
                                            validPowerSupplyService) {
                            if (!validPowerSupplyPath)
                            {
                                BMCWEB_LOG_ERROR
                                    << "Not a valid power supply ID:"
                                    << powerSupplyID;
                                messages::resourceNotFound(asyncResp->res,
                                                           "PowerSupply",
                                                           powerSupplyID);
                                return;
                            }

                            BMCWEB_LOG_DEBUG << "PowerSupplyID: "
                                             << powerSupplyID;

                            asyncResp->res.jsonValue["@odata.type"] =
                                "#PowerSupplyMetrics.v1_0_0.PowerSupplyMetrics";
                            asyncResp->res.jsonValue["@odata.id"] =
                                "/redfish/v1/Chassis/" + chassisID +
                                "/PowerSubsystem/PowerSupplies/" +
                                powerSupplyID + "/Metrics";
                            asyncResp->res.jsonValue["Name"] =
                                "Metrics for " + powerSupplyID;
                            asyncResp->res.jsonValue["Id"] = "Metrics";

                            asyncResp->res.jsonValue["Oem"]["@odata.type"] =
                                "#OemPowerSupplyMetrics.Oem";
                            asyncResp->res
                                .jsonValue["Oem"]["IBM"]["@odata.type"] =
                                "#OemPowerSupplyMetrics.IBM";
                            getValues(asyncResp, chassisID, powerSupplyID);
                        };
                    redfish::power_supply_utils::getValidPowerSupplyID(
                        asyncResp, chassisID, powerSupplyID,
                        std::move(getPowerSupplyHandler));
                };
                redfish::chassis_utils::getValidChassisID(
                    asyncResp, chassisID, std::move(getChassisID));
            });
}
} // namespace redfish
