#pragma once

#include "led.hpp"

#include <utils/collection.hpp>
#include <utils/fabric_util.hpp>
#include <utils/json_utils.hpp>
#include <utils/name_utils.hpp>
#include <utils/pcie_util.hpp>

namespace redfish
{

using MapperGetSubTreeResponse = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;

using ServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

using VariantType = std::variant<bool, std::string, uint64_t, uint32_t>;
using PropertyType = std::pair<std::string, VariantType>;
using PropertyListType = std::vector<PropertyType>;

/**
 * @brief Api to fetch properties of given adapter.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     Adapter Dbus object path.
 * @param[in]       serviceMap  Map to hold service and interface.
 */
inline void
    getAdapterProperties(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& objPath,
                         const ServiceMap& serviceMap)
{
    name_util::getPrettyName(aResp, objPath, serviceMap, "/Name"_json_pointer);

    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        for (const auto& interface : interfaceList)
        {
            if (interface == "xyz.openbmc_project.Inventory.Decorator.Asset")
            {
                crow::connections::systemBus->async_method_call(
                    [aResp, objPath](const boost::system::error_code ec,
                                     const PropertyListType& propertiesList) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG << "DBUS response error";
                            messages::internalError(aResp->res);
                            return;
                        }

                        for (const PropertyType& property : propertiesList)
                        {
                            if (property.first == "PartNumber")
                            {
                                const std::string* value =
                                    std::get_if<std::string>(&property.second);
                                if (value == nullptr)
                                {
                                    messages::internalError(aResp->res);
                                    return;
                                }
                                aResp->res.jsonValue["PartNumber"] = *value;
                            }
                            else if (property.first == "SerialNumber")
                            {
                                const std::string* value =
                                    std::get_if<std::string>(&property.second);
                                if (value == nullptr)
                                {
                                    messages::internalError(aResp->res);
                                    return;
                                }
                                aResp->res.jsonValue["SerialNumber"] = *value;
                            }
                            else if (property.first == "SparePartNumber")
                            {
                                const std::string* value =
                                    std::get_if<std::string>(&property.second);
                                if (value == nullptr)
                                {
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                if (!(*value).empty())
                                {
                                    aResp->res.jsonValue["SparePartNumber"] =
                                        *value;
                                }
                            }
                            else if (property.first == "Model")
                            {
                                const std::string* value =
                                    std::get_if<std::string>(&property.second);
                                if (value == nullptr)
                                {
                                    messages::internalError(aResp->res);
                                    return;
                                }
                                aResp->res.jsonValue["Model"] = *value;
                            }
                        }
                    },
                    serviceName, objPath, "org.freedesktop.DBus.Properties",
                    "GetAll",
                    "xyz.openbmc_project.Inventory.Decorator."
                    "Asset");
            }
            else if (interface == "xyz.openbmc_project.Inventory."
                                  "Decorator.LocationCode")
            {
                crow::connections::systemBus->async_method_call(
                    [aResp,
                     objPath](const boost::system::error_code ec,
                              const std::variant<std::string>& property) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG << "DBUS response error";
                            messages::internalError(aResp->res);
                            return;
                        }

                        const std::string* value =
                            std::get_if<std::string>(&property);

                        if (value == nullptr)
                        {
                            // illegal value
                            messages::internalError(aResp->res);
                            return;
                        }
                        aResp->res.jsonValue["Location"]["PartLocation"]
                                            ["ServiceLabel"] = *value;
                    },
                    serviceName, objPath, "org.freedesktop.DBus.Properties",
                    "Get",
                    "xyz.openbmc_project.Inventory.Decorator."
                    "LocationCode",
                    "LocationCode");
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Item.PCIeDevice")
            {
                // if the adapter also implements this interface, link the
                // adapter schema to PCIeDevice schema for this adapter.
                std::string devName = pcie_util::buildPCIeUniquePath(objPath);

                if (devName.empty())
                {
                    BMCWEB_LOG_ERROR << "Failed to find / in pcie device path";
                    messages::internalError(aResp->res);
                    return;
                }

                nlohmann::json& deviceArray =
                    aResp->res.jsonValue["Links"]["PCIeDevices"];
                deviceArray = nlohmann::json::array();

                deviceArray.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Systems/system/PCIeDevices/" + devName}});

                aResp->res.jsonValue["Links"]["PCIeDevices@odata.count"] =
                    deviceArray.size();
            }

            else if (interface == "xyz.openbmc_project.Inventory.Item")
            {

                crow::connections::systemBus->async_method_call(
                    [aResp, objPath](const boost::system::error_code ec,
                                     const std::variant<bool>& property) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG << "DBUS response error";
                            messages::internalError(aResp->res);
                            return;
                        }

                        const bool* present = std::get_if<bool>(&property);

                        if (present == nullptr)
                        {
                            // illegal value
                            messages::internalError(aResp->res);
                            return;
                        }
                        if (*present)
                        {
                            aResp->res.jsonValue["Status"]["State"] = "Enabled";
                        }
                        else
                        {
                            aResp->res.jsonValue["Status"]["State"] = "Absent";
                        }
                    },
                    serviceName, objPath, "org.freedesktop.DBus.Properties",
                    "Get", "xyz.openbmc_project.Inventory.Item", "Present");
            }

            else if (interface ==
                     "xyz.openbmc_project.State.Decorator.OperationalStatus")
            {

                crow::connections::systemBus->async_method_call(
                    [aResp, objPath](const boost::system::error_code ec,
                                     const std::variant<bool>& property) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG << "DBUS response error";
                            messages::internalError(aResp->res);
                            return;
                        }

                        const bool* functional = std::get_if<bool>(&property);

                        if (functional == nullptr)
                        {
                            // illegal value
                            messages::internalError(aResp->res);
                            return;
                        }
                        if (*functional)
                        {
                            aResp->res.jsonValue["Status"]["Health"] = "OK";
                        }
                        else
                        {
                            aResp->res.jsonValue["Status"]["Health"] =
                                "Critical";
                        }
                    },
                    serviceName, objPath, "org.freedesktop.DBus.Properties",
                    "Get",
                    "xyz.openbmc_project.State.Decorator.OperationalStatus",
                    "Functional");
            }
        }
    }
}

inline void getOemLocationIndicatorActive(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [objPath, aResp](const boost::system::error_code ec,
                         const std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error, ec: " << ec.value();
                return;
            }

            const std::vector<std::string>* endpoints =
                std::get_if<std::vector<std::string>>(&resp);
            if (endpoints == nullptr)
            {
                BMCWEB_LOG_DEBUG << "No endpoints, skipping get location "
                                    "indicator active";
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& endpoint : *endpoints)
            {
                bool isBlink = boost::ends_with(endpoint, "_blink");
                auto callback = [aResp, endpoint,
                                 isBlink](const std::string& serviceName) {
                    crow::connections::systemBus->async_method_call(
                        [aResp, isBlink](const boost::system::error_code ec,
                                         const std::variant<bool> asserted) {
                            if (ec)
                            {
                                BMCWEB_LOG_ERROR
                                    << "async_method_call failed with ec "
                                    << ec.value();
                                messages::internalError(aResp->res);
                                return;
                            }

                            const bool* ledOn = std::get_if<bool>(&asserted);
                            if (!ledOn)
                            {
                                BMCWEB_LOG_ERROR
                                    << "Fail to get Asserted status ";
                                messages::internalError(aResp->res);
                                return;
                            }

                            aResp->res
                                .jsonValue["Oem"]["OpenBMC"]["@odata.type"] =
                                "#OemFabricAdapter.v1_0_0.FabricAdapter";
                            aResp->res.jsonValue["Oem"]["OpenBMC"]
                                                ["LocationIndicatorActive"] =
                                *ledOn;
                        },
                        serviceName, endpoint,
                        "org.freedesktop.DBus.Properties", "Get",
                        "xyz.openbmc_project.Led.Group", "Asserted");
                };
                getLEDService(aResp, endpoint, std::move(callback));
                break;
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/identify_led_group",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Api to look for specific fabric adapter among
 * all available Fabric adapters on a system.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       adapter     Fabric adapter to look for.
 */
inline void getAdapter(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                       const std::string& adapter)
{
    aResp->res.jsonValue["@odata.type"] = "#FabricAdapter.v1_2_0.FabricAdapter";
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/system/FabricAdapters/" + adapter;

    crow::connections::systemBus->async_method_call(
        [adapter, aResp](const boost::system::error_code ec,
                         const MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DBus method call failed with error "
                                 << ec.value();

                // No adapter objects found by mapper
                if (ec.value() == boost::system::errc::io_error)
                {
                    messages::resourceNotFound(
                        aResp->res, "#FabricAdapter.v1_2_0.FabricAdapter",
                        adapter);
                    return;
                }

                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& [objectPath, serviceMap] : subtree)
            {
                std::string adapterId =
                    fabric_util::buildFabricUniquePath(objectPath);
                if (adapterId.empty())
                {
                    BMCWEB_LOG_ERROR << "Failed to find / in adapter path";
                    messages::internalError(aResp->res);
                    return;
                }

                if (adapterId != adapter)
                {
                    // this is not the adapter we are interested in
                    continue;
                }

                aResp->res.jsonValue["Id"] = adapterId;
                aResp->res.jsonValue["Ports"] = {
                    {"@odata.id", "/redfish/v1/Systems/system/FabricAdapters/" +
                                      adapterId + "/Ports"}};

                // use last part of Object path as a default name but update it
                // with PrettyName incase one is found.
                aResp->res.jsonValue["Name"] = adapterId;

                getAdapterProperties(aResp, objectPath, serviceMap);
                getOemLocationIndicatorActive(aResp, objectPath);
                return;
            }
            BMCWEB_LOG_ERROR << "Adapter not found";
            messages::resourceNotFound(aResp->res, "FabricAdapter", adapter);
            return;
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.FabricAdapter"});
}

inline void requestRoutesFabricAdapterCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/FabricAdapters/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([](const crow::Request&,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            asyncResp->res.jsonValue["@odata.type"] =
                "#FabricAdapterCollection.FabricAdapterCollection";
            asyncResp->res.jsonValue["Name"] = "Fabric adapter Collection";

            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/system/FabricAdapters";
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG << "DBUS response error";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    nlohmann::json& members =
                        asyncResp->res.jsonValue["Members"];
                    members = nlohmann::json::array();

                    for (const auto& object : objects)
                    {
                        std::string leaf =
                            fabric_util::buildFabricUniquePath(object);
                        if (leaf.empty())
                        {
                            continue;
                        }
                        std::string newPath =
                            "/redfish/v1/Systems/system/FabricAdapters";
                        newPath += '/';
                        newPath += leaf;
                        members.push_back({{"@odata.id", std::move(newPath)}});
                    }
                    asyncResp->res.jsonValue["Members@odata.count"] =
                        members.size();
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.FabricAdapter"});
        });
}

/**
 * Systems derived class for delivering Fabric adapter Schema.
 */
inline void requestRoutesFabricAdapters(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/FabricAdapters/<str>/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& fabricAdapter) {
                BMCWEB_LOG_DEBUG << "Adapter =" << fabricAdapter;
                getAdapter(asyncResp, fabricAdapter);
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/FabricAdapters/<str>/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::patch)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& fabricAdapter) {
                std::optional<bool> locationIndicatorActive;
                if (!json_util::readJson(req, asyncResp->res,
                                         "LocationIndicatorActive",
                                         locationIndicatorActive))
                {
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [fabricAdapter, locationIndicatorActive, asyncResp](
                        const boost::system::error_code ec,
                        const crow::openbmc_mapper::GetSubTreeType& subtree) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const auto& [objectPath, serviceMap] : subtree)
                        {
                            std::string adapterId =
                                sdbusplus::message::object_path(objectPath)
                                    .filename();
                            if (adapterId.empty() || adapterId != fabricAdapter)
                            {
                                continue;
                            }

                            if (locationIndicatorActive)
                            {
                                setLocationIndicatorActive(
                                    asyncResp, objectPath,
                                    *locationIndicatorActive);
                            }
                            return;
                        }
                        messages::resourceNotFound(
                            asyncResp->res, "FabricAdapter", fabricAdapter);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.FabricAdapter"});
            });
}
} // namespace redfish