#pragma once

#include <boost/container/flat_map.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/fabric_util.hpp>
#include <utils/json_utils.hpp>
#include <utils/pcie_util.hpp>

namespace redfish
{

/**
 * @brief Api to get Cable Association.
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       cableObjectPath Object path of the Cable with association.
 * @param[in]       callback        callback method
 */
template <typename Callback>
inline void
    linkAssociatedCable(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& cableObjectPath, Callback&& callback)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, callback{std::move(callback)}](
            const boost::system::error_code ec,
            const std::variant<std::vector<std::string>>& endpoints) {
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    // This cable have no association.
                    BMCWEB_LOG_DEBUG << "No association found";
                    return;
                }
                BMCWEB_LOG_ERROR << "DBUS response error" << ec.message();
                messages::internalError(asyncResp->res);
                return;
            }

            const std::vector<std::string>* value =
                std::get_if<std::vector<std::string>>(&(endpoints));

            if (value == nullptr)
            {
                BMCWEB_LOG_DEBUG << "Error getting cable association!";
                messages::internalError(asyncResp->res);
                return;
            }

            if ((*value).size() < 1)
            {
                BMCWEB_LOG_DEBUG << "No association found for cable";
                messages::internalError(asyncResp->res);
                return;
            }
            callback(*value);
        },
        "xyz.openbmc_project.ObjectMapper", cableObjectPath,
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Fill cable specific properties.
 * @param[in,out]   resp        HTTP response.
 * @param[in]       ec          Error code corresponding to Async method call.
 * @param[in]       properties  List of Cable Properties key/value pairs.
 */
inline void
    fillCableProperties(crow::Response& resp,
                        const boost::system::error_code ec,
                        const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
        messages::internalError(resp);
        return;
    }

    for (const auto& [propKey, propVariant] : properties)
    {
        if (propKey == "CableTypeDescription")
        {
            const std::string* cableTypeDescription =
                std::get_if<std::string>(&propVariant);
            if (cableTypeDescription == nullptr)
            {
                messages::internalError(resp);
                return;
            }
            resp.jsonValue["CableType"] = *cableTypeDescription;
        }
        else if (propKey == "CableStatus")
        {
            const std::string* cableStatus =
                std::get_if<std::string>(&propVariant);
            if (cableStatus == nullptr)
            {
                messages::internalError(resp);
                return;
            }

            std::string linkStatus = *cableStatus;
            if (linkStatus.empty())
            {
                continue;
            }

            if (linkStatus ==
                "xyz.openbmc_project.Inventory.Item.Cable.Status.Inactive")
            {
                resp.jsonValue["CableStatus"] = "Normal";
                resp.jsonValue["Status"]["State"] = "StandbyOffline";
                resp.jsonValue["Status"]["Health"] = "OK";
            }

            else if (linkStatus == "xyz.openbmc_project.Inventory.Item."
                                   "Cable.Status.Running")
            {
                resp.jsonValue["CableStatus"] = "Normal";
                resp.jsonValue["Status"]["State"] = "Enabled";
                resp.jsonValue["Status"]["Health"] = "OK";
            }

            else if (linkStatus == "xyz.openbmc_project.Inventory.Item."
                                   "Cable.Status.PoweredOff")
            {
                resp.jsonValue["CableStatus"] = "Disabled";
                resp.jsonValue["Status"]["State"] = "StandbyOffline";
                resp.jsonValue["Status"]["Health"] = "OK";
            }
        }
        else if (propKey == "Length")
        {
            const double* cableLength = std::get_if<double>(&propVariant);
            if (cableLength == nullptr)
            {
                messages::internalError(resp);
                return;
            }

            if (!std::isfinite(*cableLength))
            {
                if (std::isnan(*cableLength))
                {
                    continue;
                }
                messages::internalError(resp);
                return;
            }

            resp.jsonValue["LengthMeters"] = *cableLength;
        }
    }
}

/**
 * @brief Api to get Cable properties.
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       cableObjectPath Object path of the Cable.
 * @param[in]       serviceMap      A map to hold Service and corresponding
 * interface list for the given cable id.
 */
inline void
    getCableProperties(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& cableObjectPath,
                       const dbus::utility::MapperServiceMap& serviceMap)
{
    BMCWEB_LOG_DEBUG << "Get Properties for cable " << cableObjectPath;

    // retrieve Upstream Resources
    std::string upstreamResource = cableObjectPath + "/upstream_resource";
    linkAssociatedCable(
        asyncResp, upstreamResource,
        [asyncResp](const std::vector<std::string>& value) {
            nlohmann::json& linkArray =
                asyncResp->res.jsonValue["Links"]["UpstreamResources"];
            linkArray = nlohmann::json::array();

            for (const auto& fullPath : value)
            {
                std::string devName = pcie_util::buildPCIeUniquePath(fullPath);

                if (devName.empty())
                {
                    continue;
                }
                linkArray.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Systems/system/PCIeDevices/" + devName}});
            }
        });

    // retrieve Downstream Resources
    auto chassisAssociation = [asyncResp,
                               cableObjectPath](std::vector<std::string>&
                                                    updatedAssemblyList) {
        std::string downstreamResource =
            cableObjectPath + "/downstream_resource";
        linkAssociatedCable(
            asyncResp, downstreamResource,
            [asyncResp,
             updatedAssemblyList](const std::vector<std::string>& value) {
                nlohmann::json& linkArray =
                    asyncResp->res.jsonValue["Links"]["DownstreamResources"];
                linkArray = nlohmann::json::array();

                for (const auto& fullPath : value)
                {
                    auto it = find(updatedAssemblyList.begin(),
                                   updatedAssemblyList.end(), fullPath);

                    // If element was found
                    if (it != updatedAssemblyList.end())
                    {

                        uint index =
                            static_cast<uint>(it - updatedAssemblyList.begin());
                        linkArray.push_back(
                            {{"@odata.id", "/redfish/v1/Chassis/chassis/"
                                           "Assembly#/Assemblies/" +
                                               std::to_string(index)}});
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR
                            << "in Downstream Resources " << fullPath
                            << " isn't found in chassis assembly list";
                    }
                }
            });
    };

    redfish::chassis_utils::getChassisAssembly(asyncResp, "chassis",
                                               std::move(chassisAssociation));

    // retrieve Upstream Ports
    std::string upstreamConnector = cableObjectPath + "/upstream_connector";
    linkAssociatedCable(
        asyncResp, upstreamConnector,
        [asyncResp](const std::vector<std::string>& value) {
            nlohmann::json& linkArray =
                asyncResp->res.jsonValue["Links"]["UpstreamPorts"];
            linkArray = nlohmann::json::array();

            for (const auto& fullPath : value)
            {
                sdbusplus::message::object_path path(fullPath);
                std::string leaf = path.filename();
                if (leaf.empty())
                {
                    continue;
                }
                std::string parentPath = path.parent_path();
                std::string endpointLeaf =
                    fabric_util::buildFabricUniquePath(parentPath);
                if (endpointLeaf.empty())
                {
                    continue;
                }

                //  insert/create link using endpointLeaf
                endpointLeaf += "/Ports/";
                endpointLeaf += leaf;
                linkArray.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Systems/system/FabricAdapters/" +
                          endpointLeaf}});
            }
        });

    // retrieve Downstream Ports
    std::string downstreamConnector = cableObjectPath + "/downstream_connector";
    linkAssociatedCable(
        asyncResp, downstreamConnector,
        [asyncResp](const std::vector<std::string>& value) {
            nlohmann::json& linkArray =
                asyncResp->res.jsonValue["Links"]["DownstreamPorts"];
            linkArray = nlohmann::json::array();

            for (const auto& fullPath : value)
            {
                sdbusplus::message::object_path path(fullPath);
                std::string leaf = path.filename();
                if (leaf.empty())
                {
                    continue;
                }
                std::string parentPath = path.parent_path();
                std::string endpointLeaf =
                    fabric_util::buildFabricUniquePath(parentPath);
                if (endpointLeaf.empty())
                {
                    continue;
                }

                //  insert/create link suing endpointLeaf
                endpointLeaf += "/Ports/";
                endpointLeaf += leaf;
                linkArray.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Systems/system/FabricAdapters/" +
                          endpointLeaf}});
            }
        });

    // retrieve Upstream Chassis
    std::string upstreamChassis = cableObjectPath + "/upstream_chassis";
    linkAssociatedCable(
        asyncResp, upstreamChassis,
        [asyncResp](const std::vector<std::string>& value) {
            nlohmann::json& linkArray =
                asyncResp->res.jsonValue["Links"]["UpstreamChassis"];
            linkArray = nlohmann::json::array();

            for (const auto& fullPath : value)
            {
                sdbusplus::message::object_path path(fullPath);
                std::string leaf = path.filename();
                if (leaf.empty())
                {
                    continue;
                }
                linkArray.push_back(
                    {{"@odata.id", "/redfish/v1/Chassis/" + leaf}});
            }
        });

    // retrieve Downstream Chassis
    std::string downstreamChassis = cableObjectPath + "/downstream_chassis";
    linkAssociatedCable(
        asyncResp, downstreamChassis,
        [asyncResp](const std::vector<std::string>& value) {
            nlohmann::json& linkArray =
                asyncResp->res.jsonValue["Links"]["DownstreamChassis"];
            linkArray = nlohmann::json::array();
            for (const auto& fullPath : value)
            {
                sdbusplus::message::object_path path(fullPath);
                std::string leaf = path.filename();
                if (leaf.empty())
                {
                    continue;
                }
                linkArray.push_back(
                    {{"@odata.id", "/redfish/v1/Chassis/" + leaf}});
            }
        });

    for (const auto& [service, interfaces] : serviceMap)
    {
        for (const auto& interface : interfaces)
        {
            if (interface != "xyz.openbmc_project.Inventory.Item.Cable")
            {
                continue;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp](
                    const boost::system::error_code ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
                    fillCableProperties(asyncResp->res, ec, properties);
                },
                service, cableObjectPath, "org.freedesktop.DBus.Properties",
                "GetAll", interface);

            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            const std::variant<std::string>& partNumber) {
                    if (ec)
                    {
                        if (ec.value() == EBADR)
                        {
                            return;
                        }
                        BMCWEB_LOG_ERROR << "On Decorator.Asset interface "
                                            "PartNumber DBUS response error "
                                         << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const std::string* partNumberPtr =
                        std::get_if<std::string>(&partNumber);
                    if (partNumberPtr != nullptr)
                    {
                        asyncResp->res.jsonValue["PartNumber"] = *partNumberPtr;
                    }
                },
                service, cableObjectPath, "org.freedesktop.DBus.Properties",
                "Get", "xyz.openbmc_project.Inventory.Decorator.Asset",
                "PartNumber");
        }
    }
}

/**
 * The Cable schema
 */
inline void requestRoutesCable(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Cables/<str>/")
        .privileges(redfish::privileges::getCable)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& cableId) {
                BMCWEB_LOG_DEBUG << "Cable Id: " << cableId;
                auto respHandler =
                    [asyncResp,
                     cableId](const boost::system::error_code ec,
                              const dbus::utility::MapperGetSubTreeResponse&
                                  subtree) {
                        if (ec.value() == EBADR)
                        {
                            messages::resourceNotFound(
                                asyncResp->res, "#Cable.v1_2_0.Cable", cableId);
                            return;
                        }

                        if (ec)
                        {
                            BMCWEB_LOG_ERROR << "DBUS response error " << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const auto& [objectPath, serviceMap] : subtree)
                        {
                            sdbusplus::message::object_path path(objectPath);
                            if (path.filename() != cableId)
                            {
                                continue;
                            }

                            asyncResp->res.jsonValue["@odata.type"] =
                                "#Cable.v1_2_0.Cable";
                            asyncResp->res.jsonValue["@odata.id"] =
                                "/redfish/v1/Cables/" + cableId;
                            asyncResp->res.jsonValue["Id"] = cableId;
                            asyncResp->res.jsonValue["Name"] = "Cable";

                            getCableProperties(asyncResp, objectPath,
                                               serviceMap);
                            return;
                        }
                        messages::resourceNotFound(asyncResp->res, "Cable",
                                                   cableId);
                    };

                crow::connections::systemBus->async_method_call(
                    respHandler, "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Cable"});
            });
}

/**
 * Collection of Cable resource instances
 */
inline void requestRoutesCableCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Cables/")
        .privileges(redfish::privileges::getCableCollection)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                asyncResp->res.jsonValue["@odata.type"] =
                    "#CableCollection.CableCollection";
                asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Cables";
                asyncResp->res.jsonValue["Name"] = "Cable Collection";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of Cable Entries";

                collection_util::getCollectionMembers(
                    asyncResp, "/redfish/v1/Cables",
                    {"xyz.openbmc_project.Inventory.Item.Cable"});
            });
}

} // namespace redfish
