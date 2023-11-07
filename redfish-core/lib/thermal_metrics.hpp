#pragma once

#include "dbus_utility.hpp"
#include "sensors.hpp"

namespace redfish
{

/**
 * @brief Builds a json sensor representation of a sensor.
 * @param sensorName  The name of the sensor to be built
 * @param sensorType  The type (temperature, fan_tach, etc) of the sensor to
 * build
 * @param sensorsAsyncResp  Sensor metadata
 * @param interfacesDict  A dictionary of the interfaces and properties of said
 * interfaces to be built from
 * @param sensorJson  The json object to fill
 */
inline void metricsObjectInterfacesToJson(
    const std::string& sensorName, const std::string& sensorType,
    const std::shared_ptr<SensorsAsyncResp>& sensorsAsyncResp,
    const dbus::utility::DBusInteracesMap& interfacesDict,
    nlohmann::json& sensorJson)
{
    // We need a value interface before we can do anything with it
    for (const auto& [interface, values] : interfacesDict)
    {
        if (interface != "xyz.openbmc_project.Sensor.Value")
        {
            continue;
        }

        // Assume values exist as is (10^0 == 1) if no scale exists
        int64_t scaleMultiplier = 0;
        dbus::utility::DbusVariantType valueVariant;
        for (const auto& [property, value] : values)
        {
            if (property == "Scale")
            {
                // If a scale exists, pull value as int64, and use the scaling.
                const int64_t* int64Value = std::get_if<int64_t>(&value);
                if (int64Value != nullptr)
                {
                    scaleMultiplier = *int64Value;
                }
            }
            else if (property == "Value")
            {
                valueVariant = value;
            }
        }

        nlohmann::json::json_pointer unit("/Reading");
        if (sensorsAsyncResp->chassisSubNode == sensors::node::thermal)
        {
            if (sensorType == "temperature")
            {
                unit = "/Reading"_json_pointer;
            }
        }

        // The property we want to set may be nested json, so use
        // a json_pointer for easy indexing into the json structure.
        const nlohmann::json::json_pointer& key = unit;

        // Attempt to pull the int64 directly
        const int64_t* int64Value = std::get_if<int64_t>(&valueVariant);

        const double* doubleValue = std::get_if<double>(&valueVariant);
        const uint32_t* uValue = std::get_if<uint32_t>(&valueVariant);
        double temp = 0.0;
        if (int64Value != nullptr)
        {
            temp = static_cast<double>(*int64Value);
        }
        else if (doubleValue != nullptr)
        {
            temp = *doubleValue;
        }
        else if (uValue != nullptr)
        {
            temp = *uValue;
        }
        else
        {
            BMCWEB_LOG_ERROR << "Got value interface that wasn't int or double";
            continue;
        }
        temp = temp * std::pow(10, scaleMultiplier);
        sensorJson[key] = temp;

        sensorJson["DataSourceUri"] = crow::utility::urlFromPieces(
            "redfish", "v1", "Chassis", sensorsAsyncResp->chassisId, "Sensors",
            sensorName);
        sensorJson["@odata.id"] = crow::utility::urlFromPieces(
            "redfish", "v1", "Chassis", sensorsAsyncResp->chassisId, "Sensors",
            sensorName);
        std::string path = "/xyz/openbmc_project/sensors/";
        path += sensorType;
        path += "/";
        path += sensorName;
        sensorsAsyncResp->addMetadata(sensorJson, path);

        BMCWEB_LOG_DEBUG << "Added sensor " << sensorName;
        return;
    }
    BMCWEB_LOG_ERROR << "Sensor doesn't have a value interface";
}

/**
 * @brief Retrieves requested chassis sensors and redundancy data from DBus .
 * @param sensorsAsyncResp   Pointer to object holding response data
 * @param callback  Callback for next step in gathered sensor processing
 */
template <typename Callback>
void getThermalMetrics(
    const std::shared_ptr<SensorsAsyncResp>& sensorsAsyncResp,
    Callback&& callback)
{
    constexpr std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    auto respHandler =
        [callback{std::forward<Callback>(callback)},
         sensorsAsyncResp](const boost::system::error_code ec,
                           const std::vector<std::string>& chassisPaths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "getThermalMetrics respHandler DBUS error: "
                             << ec;
            messages::internalError(sensorsAsyncResp->asyncResp->res);
            return;
        }

        const std::string* chassisPath = nullptr;
        std::string chassisName;
        for (const std::string& chassis : chassisPaths)
        {
            sdbusplus::message::object_path path(chassis);
            chassisName = path.filename();
            if (chassisName.empty())
            {
                BMCWEB_LOG_ERROR << "Failed to find '/' in " << chassis;
                continue;
            }
            if (chassisName == sensorsAsyncResp->chassisId)
            {
                chassisPath = &chassis;
                break;
            }
        }
        if (chassisPath == nullptr)
        {
            messages::resourceNotFound(sensorsAsyncResp->asyncResp->res,
                                       "ThermalMetrics",
                                       sensorsAsyncResp->chassisId);
            return;
        }

        sensorsAsyncResp->asyncResp->res.jsonValue["@odata.type"] =
            "#ThermalMetrics.v1_0_0.ThermalMetrics";
        sensorsAsyncResp->asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Chassis/" + sensorsAsyncResp->chassisId +
            "/ThermalSubsystem/ThermalMetrics";
        sensorsAsyncResp->asyncResp->res.jsonValue["Id"] = "ThermalMetrics";
        sensorsAsyncResp->asyncResp->res.jsonValue["Name"] =
            "Chassis Thermal Metrics";
        sensorsAsyncResp->asyncResp->res
            .jsonValue["TemperatureReadingsCelsius"] = nlohmann::json::array();

        // Get the list of all sensors for this Chassis element
        dbus::utility::getAssociationEndPoints(
            *chassisPath + "/all_sensors",
            [sensorsAsyncResp, callback{std::move(callback)}](
                const boost::system::error_code& ec1,
                const std::variant<std::vector<std::string>>&
                    variantEndpoints) {
            if (ec1)
            {
                if (ec1.value() == EBADR)
                {
                    return;
                }
                messages::internalError(sensorsAsyncResp->asyncResp->res);
                return;
            }
            const std::vector<std::string>* nodeSensorList =
                std::get_if<std::vector<std::string>>(&(variantEndpoints));
            if (nodeSensorList == nullptr)
            {
                messages::resourceNotFound(sensorsAsyncResp->asyncResp->res,
                                           sensorsAsyncResp->chassisSubNode,
                                           "Temperatures");
                return;
            }
            const std::shared_ptr<std::set<std::string>> culledSensorList =
                std::make_shared<std::set<std::string>>();
            reduceSensorList(sensorsAsyncResp->asyncResp->res,
                             sensorsAsyncResp->chassisSubNode,
                             sensorsAsyncResp->types, nodeSensorList,
                             culledSensorList);
            callback(culledSensorList);
        });
    };
    dbus::utility::getSubTreePaths("/xyz/openbmc_project/inventory", 0,
                                   interfaces, std::move(respHandler));
}

/**
 * @brief Gets the values of the specified sensors.
 *
 * Stores the results as JSON in the SensorsAsyncResp.
 *
 * Gets the sensor values asynchronously.  Stores the results later when the
 * information has been obtained.
 *
 * The sensorNames set contains all requested sensors for the current chassis.
 *
 * To minimize the number of DBus calls, the DBus method
 * org.freedesktop.DBus.ObjectManager.GetManagedObjects() is used to get the
 * values of all sensors provided by a connection (service).
 *
 * The connections set contains all the connections that provide sensor values.
 *
 * The InventoryItem vector contains D-Bus inventory items associated with the
 * sensors.  Inventory item data is needed for some Redfish sensor properties.
 *
 * @param sensorsAsyncResp Pointer to object holding response data.
 * @param sensorNames All requested sensors within the current chassis.
 * @param connections Connections that provide sensor values.
 * @param inventoryItems Inventory items associated with the sensors.
 */
inline void getThermalSensorData(
    const std::shared_ptr<SensorsAsyncResp>& sensorsAsyncResp,
    const std::shared_ptr<std::set<std::string>>& sensorNames,
    const std::set<std::string>& connections,
    const std::shared_ptr<std::vector<InventoryItem>>& inventoryItems)
{
    // Get managed objects from all services exposing sensors
    for (const std::string& connection : connections)
    {
        // Response handler to process managed objects
        auto getManagedObjectsCb =
            [sensorsAsyncResp, sensorNames,
             inventoryItems](const boost::system::error_code ec,
                             const dbus::utility::ManagedObjectType& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "getManagedObjectsCb DBUS error: " << ec;
                messages::internalError(sensorsAsyncResp->asyncResp->res);
                return;
            }
            // Go through all objects and update response with sensor data
            for (const auto& objDictEntry : resp)
            {
                const std::string& objPath =
                    static_cast<const std::string&>(objDictEntry.first);
                BMCWEB_LOG_DEBUG << "getManagedObjectsCb parsing object "
                                 << objPath;

                std::vector<std::string> split;
                // Reserve space for
                // /xyz/openbmc_project/sensors/<name>/<subname>
                split.reserve(6);
                boost::algorithm::split(split, objPath, boost::is_any_of("/"));
                if (split.size() < 6)
                {
                    BMCWEB_LOG_ERROR << "Got path that isn't long enough "
                                     << objPath;
                    continue;
                }
                // These indexes aren't intuitive, as boost::split puts an empty
                // string at the beginning
                const std::string& sensorType = split[4];
                const std::string& sensorName = split[5];
                BMCWEB_LOG_DEBUG << "sensorName " << sensorName
                                 << " sensorType " << sensorType;
                if (sensorNames->find(objPath) == sensorNames->end())
                {
                    BMCWEB_LOG_ERROR << sensorName << " not in sensor list ";
                    continue;
                }

                nlohmann::json* sensorJson = nullptr;

                if (sensorType != "temperature")
                {
                    continue;
                }

                nlohmann::json& tempArray =
                    sensorsAsyncResp->asyncResp->res
                        .jsonValue["TemperatureReadingsCelsius"];
                tempArray.push_back({{"DeviceName", sensorName}});
                sensorJson = &(tempArray.back());

                if (sensorJson != nullptr)
                {
                    metricsObjectInterfacesToJson(
                        sensorName, sensorType, sensorsAsyncResp,
                        objDictEntry.second, *sensorJson);
                }
            }
            if (sensorsAsyncResp.use_count() == 1)
            {
                sortJSONResponse(sensorsAsyncResp);
                if (sensorsAsyncResp->chassisSubNode == sensors::node::thermal)
                {
                    populateFanRedundancy(sensorsAsyncResp);
                }
            }
        };

        crow::connections::systemBus->async_method_call(
            getManagedObjectsCb, connection, "/xyz/openbmc_project/sensors",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    }
}

inline void processThermalSensorList(
    const std::shared_ptr<SensorsAsyncResp>& sensorsAsyncResp,
    const std::shared_ptr<std::set<std::string>>& sensorNames)
{
    auto getConnectionCb = [sensorsAsyncResp, sensorNames](
                               const std::set<std::string>& connections) {
        BMCWEB_LOG_DEBUG << "getConnectionCb enter";
        auto getInventoryItemsCb =
            [sensorsAsyncResp, sensorNames,
             connections](const std::shared_ptr<std::vector<InventoryItem>>&
                              inventoryItems) {
            BMCWEB_LOG_DEBUG << "getInventoryItemsCb enter";
            // Get sensor data and store results in JSON
            getThermalSensorData(sensorsAsyncResp, sensorNames, connections,
                                 inventoryItems);
            BMCWEB_LOG_DEBUG << "getInventoryItemsCb exit";
        };

        // Get inventory items associated with sensors
        getInventoryItems(sensorsAsyncResp, sensorNames,
                          std::move(getInventoryItemsCb));

        BMCWEB_LOG_DEBUG << "getConnectionCb exit";
    };

    // Get set of connections that provide sensor values
    getConnections(sensorsAsyncResp, sensorNames, std::move(getConnectionCb));
}

/**
 * @brief Entry point for retrieving sensors data related to requested
 *        chassis.
 * @param sensorsAsyncResp   Pointer to object holding response data
 */
inline void
    getThermalData(const std::shared_ptr<SensorsAsyncResp>& sensorsAsyncResp)
{
    // Get set of sensors in chassis
    getThermalMetrics(
        sensorsAsyncResp,
        [sensorsAsyncResp](
            const std::shared_ptr<std::set<std::string>>& sensorNames) {
        processThermalSensorList(sensorsAsyncResp, sensorNames);
    });
}

inline void
    doThermalMetrics(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId,
                     const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR << "Not a valid chassis ID" << chassisId;
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    auto sensorAsyncResp = std::make_shared<SensorsAsyncResp>(
        asyncResp, chassisId, sensors::dbus::sensorPaths,
        sensors::node::thermal);

    // TODO Need to get Chassis Redundancy information.
    getThermalData(sensorAsyncResp);
}

inline void
    handleThermalMetricsGet(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doThermalMetrics, asyncResp, chassisId));
}

inline void requestRoutesThermalMetrics(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/ThermalSubsystem/ThermalMetrics/")
        .privileges(redfish::privileges::getThermalMetrics)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleThermalMetricsGet, std::ref(app)));
}

} // namespace redfish
