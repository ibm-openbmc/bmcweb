#pragma once

#include <app.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/json_utils.hpp>

namespace redfish
{

inline void getFanState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& connectionName,
                        const std::string& path)
{
    // Set the default state to Enabled
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const std::variant<bool>& state) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "Can't get Fan state!";
                messages::internalError(asyncResp->res);
                return;
            }

            const bool* value = std::get_if<bool>(&state);
            if (value == nullptr)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            if (*value == false)
            {
                asyncResp->res.jsonValue["Status"]["State"] = "Absent";
            }
        },
        connectionName, path, "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Inventory.Item", "Present");
}

inline void getFanHealth(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& connectionName,
                         const std::string& path)
{
    // Set the default Health to OK
    asyncResp->res.jsonValue["Status"]["Health"] = "OK";

    const std::array<const std::string, 1> fanInterfaces = {
        "xyz.openbmc_project.Inventory.Item.Fan"};

    // filter out PLDM mapper sources
    crow::connections::systemBus->async_method_call(
        [asyncResp, &connectionName, &path](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            const std::string PLDM{"xyz.openbmc_project.PLDM"};

            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus object query failed";
                return;
            }

            bool isPldm = (object.end() !=
                           std::find_if(object.begin(), object.end(),
                                        [&PLDM](const auto& connection) {
                                            return PLDM == connection.first;
                                        }));

            if (!isPldm)
            {
                crow::connections::systemBus->async_method_call(
                    [asyncResp](const boost::system::error_code ec,
                                const std::variant<bool>& health) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG << "Can't get Fan health!";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        const bool* value = std::get_if<bool>(&health);
                        if (value == nullptr)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (*value == false)
                        {
                            asyncResp->res.jsonValue["Status"]["Health"] =
                                "Critical";
                        }
                    },
                    connectionName, path, "org.freedesktop.DBus.Properties",
                    "Get",
                    "xyz.openbmc_project.State.Decorator.OperationalStatus",
                    "Functional");
            }
            else
            {
                asyncResp->res.jsonValue["Status"]["Health"] = "Unknown";
            }
        },

        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", path, fanInterfaces);
}

inline void getFanAsset(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& connectionName,
                        const std::string& path)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string, std::variant<std::string>>>&
                        propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "Can't get fan asset! Not implemented";
                return;
            }
            for (const std::pair<std::string, std::variant<std::string>>&
                     property : propertiesList)
            {
                const std::string& propertyName = property.first;

                if ((propertyName == "PartNumber") ||
                    (propertyName == "SerialNumber") ||
                    (propertyName == "Model") ||
                    (propertyName == "SparePartNumber") ||
                    (propertyName == "Manufacturer"))
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue[propertyName] = *value;
                }
            }
        },
        connectionName, path, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Decorator.Asset");
}

inline void getFanLocation(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    const std::string locationInterface =
        "xyz.openbmc_project.Inventory.Decorator.LocationCode";
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const std::variant<std::string>& property) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "Can't get fan Location! Not implemented";
                return;
            }

            const std::string* value = std::get_if<std::string>(&property);

            if (value == nullptr)
            {
                // illegal value
                messages::internalError(aResp->res);
                return;
            }

            aResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
                *value;
        },
        connectionName, path, "org.freedesktop.DBus.Properties", "Get",
        locationInterface, "LocationCode");
}

inline void
    getFanInventoryItem(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& path,
                        const std::string& connectionName)
{
    getFanState(asyncResp, connectionName, path);
    if ("xyz.openbmc_project.PLDM" != connectionName)
    {
        getFanHealth(asyncResp, connectionName, path);
    }
    getFanAsset(asyncResp, connectionName, path);
    getFanLocation(asyncResp, connectionName, path);
    getLocationIndicatorActive(asyncResp, path);
}

inline void getValidFan(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisId)
{
    BMCWEB_LOG_DEBUG << "Get fan list associated to chassis = " << chassisId;
    const std::array<const char*, 1> fanInterfaces = {
        "xyz.openbmc_project.Inventory.Item.Fan"};
    asyncResp->res.jsonValue["@odata.type"] = "#FanCollection.FanCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisId + "/ThermalSubsystem/Fans/";
    asyncResp->res.jsonValue["Name"] = "Fan Collection";
    asyncResp->res.jsonValue["Description"] =
        "The collection of Fan resource instances " + chassisId;
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                fansubtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(asyncResp->res);
                return;
            }

            nlohmann::json& fanList = asyncResp->res.jsonValue["Members"];
            fanList = nlohmann::json::array();

            std::string newPath =
                "/redfish/v1/Chassis/" + chassisId + "/ThermalSubsystem/Fans/";

            for (const auto& [objectPath, serviceName] : fansubtree)
            {
                if (objectPath.empty() || serviceName.size() != 1)
                {
                    BMCWEB_LOG_DEBUG << "Error getting D-Bus object!";
                    messages::internalError(asyncResp->res);
                    return;
                }

                const std::string& connectionName = serviceName[0].first;
                const std::string fanPath = objectPath;
                crow::connections::systemBus->async_method_call(
                    [asyncResp, chassisId, fanPath, connectionName, &fanList,
                     newPath](const boost::system::error_code ec,
                              const std::variant<std::vector<std::string>>&
                                  endpoints) {
                        if (ec)
                        {
                            if (ec.value() == EBADR)
                            {
                                // This fan have no chassis association.
                                return;
                            }
                            BMCWEB_LOG_ERROR << "DBUS response error";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        const std::vector<std::string>* fanChassis =
                            std::get_if<std::vector<std::string>>(&(endpoints));

                        if (fanChassis == nullptr)
                        {
                            BMCWEB_LOG_ERROR
                                << "Error getting Fan association!";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        if ((*fanChassis).size() != 1)
                        {
                            BMCWEB_LOG_ERROR << "Fan association error! ";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        std::vector<std::string> chassisPath = *fanChassis;
                        sdbusplus::message::object_path path(chassisPath[0]);
                        std::string chassisName = path.filename();
                        if (chassisName != chassisId)
                        {
                            // The fan does't belong to the chassisId
                            return;
                        }
                        sdbusplus::message::object_path pathFan(fanPath);
                        std::string fanName = pathFan.filename();

                        fanList.push_back(
                            {{"@odata.id", newPath + fanName + "/"}});
                        asyncResp->res.jsonValue["Members@odata.count"] =
                            fanList.size();
                    },
                    "xyz.openbmc_project.ObjectMapper", fanPath + "/chassis",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0, fanInterfaces);
}

template <typename Callback>
inline void getValidfanId(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          const std::string& fanId, Callback&& callback)
{
    BMCWEB_LOG_DEBUG << "getValidFanId enter";

    auto respHandler =
        [callback{std::move(callback)}, asyncResp, chassisId, fanId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            BMCWEB_LOG_DEBUG << "getValidfanId respHandler enter";

            if (ec)
            {
                BMCWEB_LOG_ERROR << "getValidfanId respHandler DBUS error: "
                                 << ec;
                messages::internalError(asyncResp->res);
                return;
            }

            // Set the default value to resourceNotFound, and if we confirm that
            // fanId is correct, the error response will be cleared.
            messages::resourceNotFound(asyncResp->res, "fan", fanId);

            for (const auto& [objectPath, serviceNames] : subtree)
            {
                if (objectPath.empty() || serviceNames.size() != 1)
                {
                    BMCWEB_LOG_DEBUG << "Error getting Fan D-Bus object!";
                    messages::internalError(asyncResp->res);
                    return;
                }
                const std::string& path = objectPath;
                const std::string& connectionName = serviceNames[0].first;
                // The association of this fan is used to determine
                // whether it belongs to this ChassisId
                crow::connections::systemBus->async_method_call(
                    [callback{std::move(callback)}, asyncResp, chassisId, fanId,
                     path, connectionName](
                        const boost::system::error_code ec,
                        const std::variant<std::vector<std::string>>&
                            endpoints) {
                        if (ec)
                        {
                            if (ec.value() == EBADR)
                            {
                                // This fan have no chassis association.
                                return;
                            }

                            BMCWEB_LOG_ERROR << "DBUS response error";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        const std::vector<std::string>* fanChassis =
                            std::get_if<std::vector<std::string>>(&(endpoints));

                        if (fanChassis != nullptr)
                        {
                            std::vector<std::string> chassisPath = *fanChassis;
                            sdbusplus::message::object_path pathChassis(
                                chassisPath[0]);
                            std::string chassisName = pathChassis.filename();
                            if (chassisName != chassisId)
                            {
                                // The fan does't belong to the
                                // chassisId
                                return;
                            }
                            sdbusplus::message::object_path pathFan(path);
                            const std::string fanName = pathFan.filename();
                            if (fanName.empty())
                            {
                                BMCWEB_LOG_ERROR << "Failed to find fanName in "
                                                 << path;
                                return;
                            }

                            if (fanName == fanId)
                            {
                                // Clear resourceNotFound response
                                asyncResp->res.clear();
                                callback(path, connectionName);
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper", path + "/chassis",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        };

    // Get the fan Collection
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Inventory.Item.Fan"});
    BMCWEB_LOG_DEBUG << "getValidFanId exit";
}

inline void requestRoutesFanCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/ThermalSubsystem/Fans/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisId) {
                auto getChassisId =
                    [asyncResp,
                     chassisId](const std::optional<std::string>& chassisPath) {
                        if (!chassisPath)
                        {
                            BMCWEB_LOG_ERROR << "Not a valid chassis ID"
                                             << chassisId;
                            messages::resourceNotFound(asyncResp->res,
                                                       "Chassis", chassisId);
                            return;
                        }
                        getValidFan(asyncResp, chassisId);
                    };
                redfish::chassis_utils::getValidChassisID(
                    asyncResp, chassisId, std::move(getChassisId));
            });
}

inline void requestRoutesFan(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/ThermalSubsystem/Fans/<str>/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisId, const std::string& fanId) {
                auto getChassisId =
                    [asyncResp, chassisId,
                     fanId](const std::optional<std::string>& chassisPath) {
                        if (!chassisPath)
                        {
                            BMCWEB_LOG_ERROR << "Not a valid chassis ID"
                                             << chassisId;
                            messages::resourceNotFound(asyncResp->res,
                                                       "Chassis", chassisId);
                            return;
                        }
                        auto getFanId =
                            [asyncResp, chassisId,
                             fanId](const std::string& validFanPath,
                                    const std::string& validFanService) {
                                std::string newPath = "/redfish/v1/Chassis/" +
                                                      chassisId +
                                                      "/ThermalSubsystem/Fans/";
                                asyncResp->res.jsonValue["@odata.type"] =
                                    "#Fan.v1_0_0.Fan";
                                asyncResp->res.jsonValue["Name"] = fanId;
                                asyncResp->res.jsonValue["Id"] = fanId;
                                asyncResp->res.jsonValue["@odata.id"] =
                                    newPath + fanId + "/";
                                getFanInventoryItem(asyncResp, validFanPath,
                                                    validFanService);
                            };
                        // Verify that the fan has the correct chassis and
                        // whether fan has a chassis association
                        getValidfanId(asyncResp, chassisId, fanId,
                                      std::move(getFanId));
                    };
                redfish::chassis_utils::getValidChassisID(
                    asyncResp, chassisId, std::move(getChassisId));
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/ThermalSubsystem/Fans/<str>/")
        .privileges({{"Login"}})
        // TODO: Use automated PrivilegeRegistry
        // Need to wait for Redfish to release a new registry
        .methods(boost::beast::http::verb::patch)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisId, const std::string& fanId) {
                std::optional<bool> locationIndicatorActive;
                if (!json_util::readJson(req, asyncResp->res,
                                         "LocationIndicatorActive",
                                         locationIndicatorActive))
                {
                    return;
                }

                if (locationIndicatorActive)
                {

                    auto getChassisId =
                        [asyncResp, chassisId, fanId, locationIndicatorActive](
                            const std::optional<std::string>& validChassisId) {
                            if (!validChassisId)
                            {
                                BMCWEB_LOG_ERROR << "Not a valid chassis Id:"
                                                 << chassisId;
                                messages::resourceNotFound(
                                    asyncResp->res, "Chassis", chassisId);
                                return;
                            }
                            auto getFanHandler =
                                [asyncResp, chassisId, fanId,
                                 locationIndicatorActive](
                                    const std::string& validFanPath,
                                    const std::string& /*validFanService*/) {
                                    setLocationIndicatorActive(
                                        asyncResp, validFanPath,
                                        *locationIndicatorActive);
                                };
                            getValidfanId(asyncResp, chassisId, fanId,
                                          std::move(getFanHandler));
                        };
                    redfish::chassis_utils::getValidChassisID(
                        asyncResp, chassisId, std::move(getChassisId));
                }
            });
}

} // namespace redfish
