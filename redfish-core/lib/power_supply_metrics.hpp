#pragma once

#include <dbus_utility.hpp>

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

inline void
    getServicePathValues(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& objectPath,
                         const std::string& objectPath2,
                         std::string& serviceName, std::string& averagePath,
                         std::string& maximumPath)
{
    const std::array<const char*, 2> interfaces = {
        "org.open_power.Sensor.Aggregation.History.Average",
        "org.open_power.Sensor.Aggregation.History.Maximum"};

    auto getServiceAndPathHandler =
        [aResp, objectPath, objectPath2, serviceName, averagePath, maximumPath](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetObject& intfObject) mutable {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "D-Bus response error on GetObject " << ec;
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& [service, interfaceNames] : intfObject)
            {
                if (service.empty())
                {
                    BMCWEB_LOG_DEBUG << "Error getting D-Bus object!";
                    messages::internalError(aResp->res);
                    return;
                }

                for (const auto& interface : interfaceNames)
                {
                    if (interface == averageInterface)
                    {
                        if (serviceName.empty())
                        {
                            serviceName = service;
                        }
                        if (averagePath.empty())
                        {
                            averagePath = objectPath;
                        }
                    }
                    else if (interface == maximumInterface)
                    {
                        if (serviceName.empty())
                        {
                            serviceName = service;
                        }
                        if (maximumPath.empty())
                        {
                            maximumPath = objectPath;
                        }
                    }
                }
                BMCWEB_LOG_DEBUG << "serviceName: " << serviceName;
                BMCWEB_LOG_DEBUG << "averagePath: " << averagePath;
                BMCWEB_LOG_DEBUG << "maximumPath: " << maximumPath;
            }

            if (objectPath2.empty())
            {
                BMCWEB_LOG_DEBUG << "Get power supply date/average/maximum "
                                    "input power values";
                getAverageMaximumValues(aResp, serviceName, averagePath,
                                        maximumPath);
            }
            else
            {
                getServicePathValues(aResp, objectPath2, "", serviceName,
                                     averagePath, maximumPath);
            }
        };

    crow::connections::systemBus->async_method_call(
        getServiceAndPathHandler, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objectPath,
        interfaces);
}

/**
 * @brief Get power supply average, maximum and date values given chassis
 * and power supply IDs.
 *
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] inputHistoryItem - Array of object paths for input history.
 *
 * @return None.
 */
inline void getValues(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                      const std::vector<std::string>& inputHistoryItem)
{
    BMCWEB_LOG_DEBUG << "ENTER: getValues(...)";
    for (const auto& item : inputHistoryItem)
    {
        BMCWEB_LOG_DEBUG << " inputHistoryItem: " << item;
    }

    // Setup InputPowerHistoryItems values array.
    // It will have 0 to many date/timestamp, average, and maximum entries.
    aResp->res
        .jsonValue["Oem"]["IBM"]["InputPowerHistoryItems"]["@odata.type"] =
        "#OemPowerSupplyMetric.InputPowerHistoryItems";

    std::string serviceName;
    std::string averagePath;
    std::string maximumPath;

    getServicePathValues(aResp, inputHistoryItem[0], inputHistoryItem[1],
                         serviceName, averagePath, maximumPath);

    BMCWEB_LOG_DEBUG << "EXIT: getValues(...)";
}

/**
 * @brief Retrieves valid input history item.
 *
 * Not all power supplies support the power input history. Do not provide
 * Redfish fields for input power history if no associated endpoint matches this
 * chassis.
 *
 * @param asyncResp     Pointer to object holding response data
 * @param powerSupplyPath Validated power supply path
 * @param callback      Callback for next step to populate Redfish JSON.
 */
template <typename Callback>
inline void
    getValidInputHistory(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& powerSupplyPath,
                         Callback&& callback)
{
    BMCWEB_LOG_DEBUG << "getValidInputHistory enter";
    BMCWEB_LOG_DEBUG << "powerSupplyPath: " << powerSupplyPath;

    auto respHandler =
        [callback{std::move(callback)}, asyncResp, powerSupplyPath](
            const boost::system::error_code ec,
            const std::variant<std::vector<std::string>>& endpoints) {
            BMCWEB_LOG_DEBUG << "getValidInputHistory respHandler enter";

            if (ec)
            {
                BMCWEB_LOG_ERROR
                    << "getValidInputHistory respHandler D-Bus error: " << ec;
                messages::internalError(asyncResp->res);
                return;
            }

            // Set the default value to resourceNotFound, and if we confirm
            // finding association with chassisID and powerSupplyID is correct,
            // the error response will be cleared.
            messages::resourceNotFound(asyncResp->res, "PowerSupplyMetrics",
                                       "Metrics");

            const std::vector<std::string>* inputHistoryItem =
                std::get_if<std::vector<std::string>>(&(endpoints));

            if (inputHistoryItem != nullptr)
            {
                if ((*inputHistoryItem).size() == 0)
                {
                    BMCWEB_LOG_ERROR
                        << "Input history item association error! ";
                    messages::internalError(asyncResp->res);
                    return;
                }

                // Clear resourceNotFound response
                asyncResp->res.clear();

                std::vector<std::string> inputHistoryPath;
                for (const auto& objpath : *inputHistoryItem)
                {
                    BMCWEB_LOG_DEBUG << "objpath: " << objpath;
                    inputHistoryPath.push_back(objpath);
                }

                callback(inputHistoryPath);
            }
        };

    // Attempt to get the input history items from associations
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.ObjectMapper",
        powerSupplyPath + "/input_history", "org.freedesktop.DBus.Properties",
        "Get", "xyz.openbmc_project.Association", "endpoints");

    BMCWEB_LOG_DEBUG << "getValidInputHistory exit";
}

/**
 * Systems derived class for delivering OemPowerSupplyMetrics Schema.
 */
inline void requestRoutesPowerSupplyMetrics(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PowerSubsystem/"
                      "PowerSupplies/<str>/Metrics")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([](const crow::Request&,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisID,
                        const std::string& powerSupplyID) {
            auto getChassisID = [asyncResp, chassisID, powerSupplyID](
                                    const std::optional<std::string>&
                                        validChassisID) {
                if (!validChassisID)
                {
                    BMCWEB_LOG_ERROR << "Not a valid chassis ID:" << chassisID;
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisID);
                    return;
                }

                BMCWEB_LOG_DEBUG << "ChassisID: " << chassisID;

                auto getPowerSupplyHandler =
                    [asyncResp, chassisID, powerSupplyID](
                        const std::optional<std::string>& validPowerSupplyPath,
                        [[maybe_unused]] const std::string&
                            validPowerSupplyService) {
                        if (!validPowerSupplyPath)
                        {
                            BMCWEB_LOG_ERROR << "Not a valid power supply ID:"
                                             << powerSupplyID;
                            messages::resourceNotFound(
                                asyncResp->res, "PowerSupply", powerSupplyID);
                            return;
                        }

                        BMCWEB_LOG_DEBUG << "PowerSupplyID: " << powerSupplyID;
                        BMCWEB_LOG_DEBUG << "validPowerSupplyPath: "
                                         << *validPowerSupplyPath;

                        auto getInputHistoryItemHandler =
                            [asyncResp, chassisID, powerSupplyID,
                             validPowerSupplyPath](
                                const std::optional<std::vector<std::string>>&
                                    validInputHistoryItem) {
                                if (!validInputHistoryItem)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "Not a valid input history item:";
                                    messages::resourceNotFound(asyncResp->res,
                                                               powerSupplyID,
                                                               "Metrics");
                                    return;
                                }

                                for (const auto& objpath :
                                     *validInputHistoryItem)
                                {
                                    BMCWEB_LOG_DEBUG
                                        << "validInputHistoryItemPath: "
                                        << objpath;
                                }

                                asyncResp->res.jsonValue["@odata.type"] =
                                    "#PowerSupplyMetrics.v1_0_0."
                                    "PowerSupplyMetrics";
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
                                getValues(asyncResp, *validInputHistoryItem);
                            };
                        getValidInputHistory(
                            asyncResp, *validPowerSupplyPath,
                            std::move(getInputHistoryItemHandler));
                    };
                redfish::power_supply_utils::getValidPowerSupplyID(
                    asyncResp, chassisID, powerSupplyID,
                    std::move(getPowerSupplyHandler));
            };
            redfish::chassis_utils::getValidChassisID(asyncResp, chassisID,
                                                      std::move(getChassisID));
        });
}
} // namespace redfish
