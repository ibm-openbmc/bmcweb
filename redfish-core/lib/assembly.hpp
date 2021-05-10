#pragma once

#include <node.hpp>
#include <utils/json_utils.hpp>

namespace redfish
{
using MapperGetSubTreeResponse = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;

/**
 * @brief Retrieves properties over dbus for inventory items which
 * are to be published as assembly on a given chassis
 *
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] basePath - Chassis path following which assmeblies are to be
 * looked.
 *
 * @return None.
 */
inline void
    getAssembliesLinkedToChassis(const std::shared_ptr<AsyncResp>& aResp,
                                 const std::string& basePath)
{
    BMCWEB_LOG_DEBUG << "Get properties for assembly associated to chassis = "
                     << basePath;

    aResp->res.jsonValue["Assemblies"] = nlohmann::json::array();

    std::string chassisID =
        sdbusplus::message::object_path(basePath).filename();
    if (chassisID.empty())
    {
        BMCWEB_LOG_ERROR << "Failed to find / in Chassis path";
        messages::internalError(aResp->res);
        return;
    }

    crow::connections::systemBus->async_method_call(
        [aResp, chassisID](const boost::system::error_code ec,
                           MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);
                return;
            }

            nlohmann::json& assemblyArray = aResp->res.jsonValue["Assemblies"];
            BMCWEB_LOG_DEBUG << "Number of assembly found = " << subtree.size();
            aResp->res.jsonValue["Assemblies@odata.count"] = subtree.size();

            std::sort(subtree.begin(), subtree.end());

            std::size_t assemblyIndex = 0;
            for (const auto& [objectPath, serviceMap] : subtree)
            {
                for (const auto& [serviceName, interfaceList] : serviceMap)
                {
                    std::string dataID = "/redfish/v1/Chassis/" + chassisID +
                                         "/Assembly#/Assemblies/";
                    dataID.append(std::to_string(assemblyIndex));
                    assemblyArray.push_back(
                        {{"@odata.type", "#Assembly.v1_3_0.AssemblyData"},
                         {"@odata.id", dataID},
                         {"MemberId", std::to_string(assemblyIndex)}});

                    // use last part of Object path as a default name but update
                    // it with PrettyName incase one is found.
                    assemblyArray.at(assemblyIndex)["Name"] =
                        sdbusplus::message::object_path(objectPath).filename();

                    for (const auto interface : interfaceList)
                    {
                        if (interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Asset")
                        {
                            crow::connections::systemBus->async_method_call(
                                [aResp, &assemblyArray, assemblyIndex](
                                    const boost::system::error_code ec2,
                                    const std::vector<
                                        std::pair<std::string, VariantType>>&
                                        propertiesList) {
                                    if (ec2)
                                    {
                                        BMCWEB_LOG_DEBUG
                                            << "DBUS response error";
                                        messages::internalError(aResp->res);
                                        return;
                                    }

                                    nlohmann::json& assemblyData =
                                        assemblyArray.at(assemblyIndex);

                                    for (const std::pair<std::string,
                                                         VariantType>&
                                             property : propertiesList)
                                    {
                                        if (property.first == "PartNumber")
                                        {
                                            const std::string* value =
                                                std::get_if<std::string>(
                                                    &property.second);
                                            if (value == nullptr)
                                            {
                                                messages::internalError(
                                                    aResp->res);
                                                return;
                                            }
                                            assemblyData["PartNumber"] = *value;
                                        }
                                        else if (property.first ==
                                                 "SerialNumber")
                                        {
                                            const std::string* value =
                                                std::get_if<std::string>(
                                                    &property.second);
                                            if (value == nullptr)
                                            {
                                                messages::internalError(
                                                    aResp->res);
                                                return;
                                            }
                                            assemblyData["SerialNumber"] =
                                                *value;
                                        }
                                        else if (property.first ==
                                                 "SparePartNumber")
                                        {
                                            const std::string* value =
                                                std::get_if<std::string>(
                                                    &property.second);
                                            if (value == nullptr)
                                            {
                                                messages::internalError(
                                                    aResp->res);
                                                return;
                                            }
                                            assemblyData["SparePartNumber"] =
                                                *value;
                                        }
                                        else if (property.first == "Model")
                                        {
                                            const std::string* value =
                                                std::get_if<std::string>(
                                                    &property.second);
                                            if (value == nullptr)
                                            {
                                                messages::internalError(
                                                    aResp->res);
                                                return;
                                            }
                                            assemblyData["Model"] = *value;
                                        }
                                    }
                                },
                                serviceName, objectPath,
                                "org.freedesktop.DBus.Properties", "GetAll",
                                "xyz.openbmc_project.Inventory.Decorator."
                                "Asset");
                        }
                        else if (interface == "xyz.openbmc_project.Inventory."
                                              "Decorator.LocationCode")
                        {
                            crow::connections::systemBus->async_method_call(
                                [aResp, &assemblyArray, assemblyIndex](
                                    const boost::system::error_code ec,
                                    const std::variant<std::string>& property) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_DEBUG
                                            << "DBUS response error";
                                        messages::internalError(aResp->res);
                                        return;
                                    }

                                    nlohmann::json& assemblyData =
                                        assemblyArray.at(assemblyIndex);

                                    const std::string* value =
                                        std::get_if<std::string>(&property);

                                    if (value == nullptr)
                                    {
                                        // illegal value
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    assemblyData["Location"]["PartLocation"]
                                                ["ServiceLabel"] = *value;
                                },
                                serviceName, objectPath,
                                "org.freedesktop.DBus.Properties", "Get",
                                "xyz.openbmc_project.Inventory.Decorator."
                                "LocationCode",
                                "LocationCode");
                        }
                        else if (interface ==
                                 "xyz.openbmc_project.Inventory.Item")
                        {
                            crow::connections::systemBus->async_method_call(
                                [aResp, &assemblyArray, assemblyIndex](
                                    const boost::system::error_code ec,
                                    const std::variant<std::string>& property) {
                                    if (ec)
                                    {
                                        // in case we do not find property
                                        // Pretty Name, we don't log error as we
                                        // already have updated name with object
                                        // path. This property is optional.
                                        BMCWEB_LOG_DEBUG << "No implementation "
                                                            "of Pretty Name";
                                        return;
                                    }

                                    nlohmann::json& assemblyData =
                                        assemblyArray.at(assemblyIndex);

                                    const std::string* value =
                                        std::get_if<std::string>(&property);

                                    if (value == nullptr)
                                    {
                                        // illegal value
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    else if (!(*value).empty())
                                    {
                                        assemblyData["Name"] = *value;
                                    }
                                },
                                serviceName, objectPath,
                                "org.freedesktop.DBus.Properties", "Get",
                                "xyz.openbmc_project.Inventory.Item",
                                "PrettyName");
                        }
                    }
                }
                assemblyIndex++;
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", basePath, 0,
        std::array<const char*, 4>{
            "xyz.openbmc_project.Inventory.Item.Vrm",
            "xyz.openbmc_project.Inventory.Item.Tpm",
            "xyz.openbmc_project.Inventory.Item.Panel",
            "xyz.openbmc_project.Inventory.Item.Battery",
        });
}

/**
 * @brief Get chassis path with given chassis ID
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisID - Chassis to which the assemblies are
 * associated.
 *
 * @return None.
 */
inline void getChassis(const std::shared_ptr<AsyncResp>& aResp,
                       const std::string& chassisID)
{
    BMCWEB_LOG_DEBUG << "Get chassis path";

    // get the chassis path
    crow::connections::systemBus->async_method_call(
        [aResp, chassisID(std::string(chassisID))](
            const boost::system::error_code ec,
            const std::vector<std::string>& chassisPaths) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);
                return;
            }

            // check if the chassis path belongs to the chassis ID passed
            for (const auto& path : chassisPaths)
            {
                BMCWEB_LOG_DEBUG << "Chassis Paths from Mapper " << path;
                std::string chassis =
                    sdbusplus::message::object_path(path).filename();
                if (chassis != chassisID)
                {
                    // this is not the chassis we are interested in
                    continue;
                }

                aResp->res.jsonValue["@odata.type"] =
                    "#Assembly.v1_3_0.Assembly";
                aResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Chassis/" + chassisID + "/Assembly";
                aResp->res.jsonValue["Name"] = "Assembly Collection";
                aResp->res.jsonValue["Id"] = "Assembly";

                getAssembliesLinkedToChassis(aResp, path);
                return;
            }

            BMCWEB_LOG_ERROR << "Chassis not found";
            messages::resourceNotFound(aResp->res, "Chassis", chassisID);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{"xyz.openbmc_project.Inventory.Item.Chassis",
                                   "xyz.openbmc_project.Inventory.Item.Board"});
}

class Assembly : public Node
{
  public:
    /*
     * Default Constructor
     */
    Assembly(App& app) :
        Node(app, "/redfish/v1/Chassis/<str>/Assembly/", std::string())
    {
        entityPrivileges = {
            {boost::beast::http::verb::get, {{"Login"}}},
            {boost::beast::http::verb::head, {{"Login"}}},
            {boost::beast::http::verb::patch, {{"ConfigureComponents"}}},
            {boost::beast::http::verb::put, {{"ConfigureComponents"}}},
            {boost::beast::http::verb::delete_, {{"ConfigureComponents"}}},
            {boost::beast::http::verb::post, {{"ConfigureComponents"}}}};
    }

  private:
    /**
     * Function to generate Assembly schema, This schema is used
     * to represent inventory items in Redfish when there is no
     * specific schema definition available
     */
    void doGet(crow::Response& res, const crow::Request&,
               const std::vector<std::string>& params) override
    {
        if (params.size() != 1)
        {
            messages::internalError(res);
            res.end();
            return;
        }
        const std::string& chassisID = params[0];
        BMCWEB_LOG_DEBUG << "Chassis ID that we got called for " << chassisID;
        std::shared_ptr<AsyncResp> asyncResp = std::make_shared<AsyncResp>(res);
        getChassis(asyncResp, chassisID);
    }
};

} // namespace redfish