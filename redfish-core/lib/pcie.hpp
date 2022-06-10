/*
// Copyright (c) 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#pragma once

#include "pcie_slots.hpp"

#include <app.hpp>
#include <boost/system/linux_error.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/pcie_util.hpp>

#include <map>

namespace redfish
{

static constexpr char const* pcieRootPath = "/xyz/openbmc_project/inventory";
static constexpr char const* pcieDeviceInterface =
    "xyz.openbmc_project.Inventory.Item.PCIeDevice";
static constexpr char const* pcieSlotInterface =
    "xyz.openbmc_project.Inventory.Item.PCIeSlot";

using PCIeDevice = std::string;
using PCIeDevicePath = std::string;
using ServiceName = std::string;
using MapperGetSubTreeResponse = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;
using MapperGetSubTreePathsResponse = std::vector<std::string>;

// will be used when we need to fetch single PCIeDevice properties.
std::map<PCIeDevice, std::pair<ServiceName, PCIeDevicePath>>
    mapOfPcieDevicePaths;

static inline void
    retrievePCIeDeviceList(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    auto getPCIeMapCallback =
        [asyncResp](const boost::system::error_code ec,
                    const MapperGetSubTreeResponse& subTree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "no PCIe device paths found ec: "
                                 << ec.message();
                // Not an error, system just doesn't have PCIe info
                return;
            }

            for (const auto& [pcieDevicePath, serviceMap] : subTree)
            {
                for (const auto& [serviceName, interfaceList] : serviceMap)
                {
                    std::string devName =
                        pcie_util::buildPCIeUniquePath(pcieDevicePath);

                    if (devName.empty())
                    {
                        BMCWEB_LOG_DEBUG << "Invalid Name";
                        continue;
                    }

                    mapOfPcieDevicePaths.emplace(
                        devName, std::make_pair(serviceName, pcieDevicePath));
                }
            }
        };
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeMapCallback), "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        std::string(pcieRootPath), 0,
        std::array<const char*, 1>{pcieDeviceInterface});
}

static inline void
    getPCIeDeviceList(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& name)
{

    retrievePCIeDeviceList(asyncResp);
    nlohmann::json& pcieDeviceList = asyncResp->res.jsonValue[name];
    pcieDeviceList = nlohmann::json::array();

    for (const auto& pcieDevice : mapOfPcieDevicePaths)
    {
        pcieDeviceList.push_back(
            {{"@odata.id",
              "/redfish/v1/Systems/system/PCIeDevices/" + pcieDevice.first}});
    }
    asyncResp->res.jsonValue[name + "@odata.count"] =
        mapOfPcieDevicePaths.size();
}
/**
 * @brief Fill PCIeDevice Status and Health based on PCIeSlot Link Status
 *
 * @param[in,out]   resp        HTTP response.
 * @param[in]       linkStatus  PCIeSlot Link Status.
 */
static inline void fillPcieDeviceStatus(crow::Response& resp,
                                        const std::string& linkStatus)
{
    if (linkStatus ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Operational")
    {
        resp.jsonValue["Status"]["State"] = "Enabled";
        resp.jsonValue["Status"]["Health"] = "OK";
        return;
    }

    if (linkStatus ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Degraded")
    {
        resp.jsonValue["Status"]["State"] = "Enabled";
        resp.jsonValue["Status"]["Health"] = "Critical";
        return;
    }

    if (linkStatus ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Failed")
    {
        resp.jsonValue["Status"]["State"] = "UnavailableOffline";
        resp.jsonValue["Status"]["Health"] = "Warning";
        return;
    }

    if (linkStatus ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Inactive")
    {
        resp.jsonValue["Status"]["State"] = "StandbyOffline";
        resp.jsonValue["Status"]["Health"] = "OK";
        return;
    }

    if (linkStatus == "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Open")
    {
        resp.jsonValue["Status"]["State"] = "Absent";
        resp.jsonValue["Status"]["Health"] = "OK";
        return;
    }
}

/**
 * @brief Get PCIe Slot properties.
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       pcieSlotPath    Object path of the PCIeSlot.
 * @param[in]       serviceMap      A map to hold Service and corresponding
 * interface list for the given cable id.
 */
static inline void
    getPcieSlotProperties(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& pcieSlotPath,
                          const dbus::utility::MapperServiceMap& serviceMap)
{
    for (const auto& [service, interfaces] : serviceMap)
    {
        for (const auto& interface : interfaces)
        {
            if ((interface == "xyz.openbmc_project.Inventory.Item.PCIeSlot") ||
                interface == "com.ibm.Control.Host.PCIeLink")
            {
                crow::connections::systemBus->async_method_call(
                    [asyncResp](
                        const boost::system::error_code ec,
                        const dbus::utility::DBusPropertiesMap& properties) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const auto& [propKey, propVariant] : properties)
                        {
                            if (propKey == "LinkStatus")
                            {
                                const std::string* value =
                                    std::get_if<std::string>(&propVariant);
                                if (value == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                std::string linkStatus = *value;
                                if (!linkStatus.empty())
                                {
                                    fillPcieDeviceStatus(asyncResp->res,
                                                         linkStatus);
                                    return;
                                }
                            }

                            if (propKey == "linkReset")
                            {
                                const bool* value =
                                    std::get_if<bool>(&propVariant);
                                if (value == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                asyncResp->res
                                    .jsonValue["Oem"]["IBM"]["LinkReset"] =
                                    *value;
                                asyncResp->res.jsonValue["Oem"]["@odata.type"] =
                                    "#OemPCIeDevice.Oem";
                                asyncResp->res
                                    .jsonValue["Oem"]["IBM"]["@odata.type"] =
                                    "#OemPCIeDevice.IBM";
                            }
                        }
                    },
                    service, pcieSlotPath, "org.freedesktop.DBus.Properties",
                    "GetAll", interface);
            }
        }
    }
}

/**
 * @brief Get subtree map for PCIeSlots.
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       pcieSlotPath    Object path of the PCIeSlot.
 * @param[in]       pcieDevice      PCIe device name/ID.
 * @param[in]       callback        Callback method.
 */
template <typename Callback>
static inline void
    findPcieSlotServiceMap(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& pcieSlotPath,
                           const std::string& pcieDevice, Callback&& callback)
{
    auto respHandler = [asyncResp, pcieSlotPath, pcieDevice,
                        callback{std::move(callback)}](
                           const boost::system::error_code ec,
                           const MapperGetSubTreeResponse& subTree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "DBUS response error on GetSubTree"
                             << ec.message();
            messages::internalError(asyncResp->res);
            return;
        }

        if (subTree.size() == 0)
        {
            BMCWEB_LOG_ERROR << "Can't find PCIeSlot D-Bus object!";
            return;
        }

        for (const auto& [objectPath, serviceMap] : subTree)
        {
            if (objectPath.empty() || serviceMap.size() != 1)
            {
                BMCWEB_LOG_ERROR << "Error getting PCIeSlot D-Bus object!";
                messages::internalError(asyncResp->res);
                return;
            }

            if (pcieSlotPath != objectPath)
            {
                continue;
            }

            callback(pcieSlotPath, serviceMap);
            return;
        }
        BMCWEB_LOG_ERROR << "PCIe Slot not found for " << pcieDevice;
    };

    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{pcieSlotInterface});
}

/**
 * @brief Helper method to find the PCIe slot path
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       pcieDevice  PCIe device name/ID.
 * @param[in]       callback    Callback method.
 */
template <typename Callback>
inline void
    findPcieSlotPath(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& pcieDevice, Callback&& callback)
{
    if (mapOfPcieDevicePaths.empty())
    {
        retrievePCIeDeviceList(asyncResp);
    }
    auto it = mapOfPcieDevicePaths.find(pcieDevice);
    if (it != mapOfPcieDevicePaths.end())
    {
        std::string& objectPath = std::get<1>(it->second);
        sdbusplus::message::object_path path(objectPath);
        std::string pcieSlotPath = path.parent_path();

        findPcieSlotServiceMap(asyncResp, pcieSlotPath, pcieDevice,
                               std::move(callback));
    }
}

/**
 * @brief Add link status and health property to PCIe device
 *
 * @param[in, out]  asyncResp   Async HTTP response.
 * @param[in]       pcieDevice  PCIe device name/ID.
 */
inline void addLinkStatusToPcieDevice(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& pcieDevice)
{
    auto linkStatus =
        [asyncResp](const std::string& pcieSlotPath,
                    const dbus::utility::MapperServiceMap& serviceMap) {
            getPcieSlotProperties(asyncResp, pcieSlotPath, serviceMap);
        };

    findPcieSlotPath(asyncResp, pcieDevice, std::move(linkStatus));
}

/**
 * @brief Set linkReset property
 *
 * @param[in, out]  asyncResp       Async HTTP response.
 * @param[in]       pcieSlotPath    PCIe slot path.
 * @param[in]       serviceMap      A map to hold Service and corresponding
 * interface list for the given cable id.
 * @param[in]       linkReset       Flag to reset.
 */
inline void handleLinkReset(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& pcieSlotPath,
                            const dbus::utility::MapperServiceMap& serviceMap,
                            const bool& linkReset)
{
    for (const auto& [service, interfaces] : serviceMap)
    {
        for (const auto& interface : interfaces)
        {
            if (interface != "com.ibm.Control.Host.PCIeLink")
            {
                continue;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, pcieSlotPath, interface,
                 linkReset](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    BMCWEB_LOG_DEBUG << "linkReset property set to: "
                                     << (linkReset ? "true" : "false");
                    return;
                },
                service, pcieSlotPath, "org.freedesktop.DBus.Properties", "Set",
                interface, "linkReset", std::variant<bool>{linkReset});
        }
    }
}

/**
 * @brief Api to reset link
 *
 * @param[in, out]  asyncResp   Async HTTP response.
 * @param[in]       pcieDevice  PCIe device name/ID.
 * @param[in]       linkReset   Flag to reset.
 */
inline void
    pcieSlotLinkReset(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& pcieDevice, const bool& linkReset)
{
    auto reset = [asyncResp, linkReset](
                     const std::string& pcieSlotPath,
                     const dbus::utility::MapperServiceMap& serviceMap) {
        handleLinkReset(asyncResp, pcieSlotPath, serviceMap, linkReset);
    };

    findPcieSlotPath(asyncResp, pcieDevice, std::move(reset));
}

inline void requestRoutesSystemPCIeDeviceCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/PCIeDevices/")
        .privileges(redfish::privileges::getPCIeDeviceCollection)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                asyncResp->res.jsonValue = {
                    {"@odata.type",
                     "#PCIeDeviceCollection.PCIeDeviceCollection"},
                    {"@odata.id", "/redfish/v1/Systems/system/PCIeDevices"},
                    {"Name", "PCIe Device Collection"},
                    {"Description", "Collection of PCIe Devices"},
                    {"Members", nlohmann::json::array()},
                    {"Members@odata.count", 0}};

                getPCIeDeviceList(asyncResp, "Members");
            });
}

inline void requestRoutesSystemPCIeDevice(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/PCIeDevices/<str>/")
        .privileges(redfish::privileges::getPCIeDevice)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& device)

            {
                auto getPCIeDeviceCallback =
                    [asyncResp,
                     device](const boost::system::error_code ec,
                             boost::container::flat_map<
                                 std::string,
                                 std::variant<std::string, size_t, bool>>&
                                 pcieDevProperties) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG
                                << "failed to get PCIe Device properties ec: "
                                << ec.value() << ": " << ec.message();
                            if (ec.value() == boost::system::linux_error::
                                                  bad_request_descriptor)
                            {
                                messages::resourceNotFound(
                                    asyncResp->res, "PCIeDevice", device);
                            }
                            else
                            {
                                messages::internalError(asyncResp->res);
                            }
                            return;
                        }

                        asyncResp->res.jsonValue = {
                            {"@odata.type", "#PCIeDevice.v1_9_0.PCIeDevice"},
                            {"@odata.id",
                             "/redfish/v1/Systems/system/PCIeDevices/" +
                                 device},
                            {"Name", "PCIe Device"},
                            {"Id", device}};

                        if (std::string* property = std::get_if<std::string>(
                                &pcieDevProperties["Manufacturer"]);
                            property)
                        {
                            asyncResp->res.jsonValue["Manufacturer"] =
                                *property;
                        }

                        if (std::string* property = std::get_if<std::string>(
                                &pcieDevProperties["DeviceType"]);
                            property)
                        {
                            if (!property->empty())
                            {
                                asyncResp->res.jsonValue["DeviceType"] =
                                    *property;
                            }
                        }

                        if (std::string* property = std::get_if<std::string>(
                                &pcieDevProperties["GenerationInUse"]);
                            property)
                        {
                            std::string generation =
                                analysisGeneration(*property);
                            if (!generation.empty())
                            {
                                asyncResp->res
                                    .jsonValue["PCIeInterface"]["PCIeType"] =
                                    generation;
                            }
                        }

                        if (std::string* property = std::get_if<std::string>(
                                &pcieDevProperties["PartNumber"]);
                            property)
                        {
                            if (!property->empty())
                            {
                                asyncResp->res.jsonValue["PartNumber"] =
                                    *property;
                            }
                        }

                        if (std::string* property = std::get_if<std::string>(
                                &pcieDevProperties["SerialNumber"]);
                            property)
                        {
                            if (!property->empty())
                            {
                                asyncResp->res.jsonValue["SerialNumber"] =
                                    *property;
                            }
                        }

                        if (std::string* property = std::get_if<std::string>(
                                &pcieDevProperties["Model"]);
                            property)
                        {
                            if (!property->empty())
                            {
                                asyncResp->res.jsonValue["Model"] = *property;
                            }
                        }

                        if (std::string* property = std::get_if<std::string>(
                                &pcieDevProperties["SparePartNumber"]);
                            property)
                        {
                            if (!property->empty())
                            {
                                asyncResp->res.jsonValue["SparePartNumber"] =
                                    *property;
                            }
                        }

                        if (std::string* property = std::get_if<std::string>(
                                &pcieDevProperties["PrettyName"]);
                            property)
                        {
                            if (!property->empty())
                            {
                                asyncResp->res.jsonValue["Name"] = *property;
                            }
                        }

                        if (std::string* property = std::get_if<std::string>(
                                &pcieDevProperties["LocationCode"]);
                            property)
                        {
                            if (!property->empty())
                            {
                                asyncResp->res
                                    .jsonValue["Slot"]["Location"]
                                              ["PartLocation"]["ServiceLabel"] =
                                    *property;
                            }
                        }

                        if (size_t* property = std::get_if<size_t>(
                                &pcieDevProperties["LanesInUse"]);
                            property)
                        {
                            if (property == nullptr)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            asyncResp->res
                                .jsonValue["PCIeInterface"]["LanesInUse"] =
                                *property;
                        }

                        // Link status
                        addLinkStatusToPcieDevice(asyncResp, device);

                        asyncResp->res.jsonValue["PCIeFunctions"] = {
                            {"@odata.id",
                             "/redfish/v1/Systems/system/PCIeDevices/" +
                                 device + "/PCIeFunctions"}};
                    };

                retrievePCIeDeviceList(asyncResp);
                auto it = mapOfPcieDevicePaths.find(device);
                if (it != mapOfPcieDevicePaths.end())
                {
                    std::string& serviceName = std::get<0>(it->second);
                    std::string& path = std::get<1>(it->second);
                    crow::connections::systemBus->async_method_call(
                        std::move(getPCIeDeviceCallback), serviceName, path,
                        "org.freedesktop.DBus.Properties", "GetAll",
                        std::string(""));
                }
                else
                {
                    messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                               device);
                    return;
                }
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/PCIeDevices/<str>/")
        .privileges(redfish::privileges::patchPCIeDevice)
        .methods(boost::beast::http::verb::patch)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& device) {
                std::optional<nlohmann::json> oem;
                if (!json_util::readJson(req, asyncResp->res, "Oem", oem))
                {
                    return;
                }

                if (oem)
                {
                    std::optional<nlohmann::json> ibmOem;
                    if (!redfish::json_util::readJson(*oem, asyncResp->res,
                                                      "IBM", ibmOem))
                    {
                        return;
                    }

                    if (ibmOem)
                    {
                        std::optional<bool> linkReset;
                        if (!json_util::readJson(*ibmOem, asyncResp->res,
                                                 "LinkReset", linkReset))
                        {
                            return;
                        }
                        pcieSlotLinkReset(asyncResp, device, *linkReset);
                    }
                }
            });
}

inline void requestRoutesSystemPCIeFunctionCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/system/PCIeDevices/<str>/PCIeFunctions/")
        .privileges(redfish::privileges::getPCIeFunctionCollection)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& device)

            {
                asyncResp->res.jsonValue = {
                    {"@odata.type",
                     "#PCIeFunctionCollection.PCIeFunctionCollection"},
                    {"@odata.id", "/redfish/v1/Systems/system/PCIeDevices/" +
                                      device + "/PCIeFunctions"},
                    {"Name", "PCIe Function Collection"},
                    {"Description",
                     "Collection of PCIe Functions for PCIe Device " + device}};

                auto getPCIeDeviceCallback = [asyncResp, device](
                                                 const boost::system::error_code
                                                     ec,
                                                 boost::container::flat_map<
                                                     std::string,
                                                     std::variant<std::string>>&
                                                     pcieDevProperties) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG
                            << "failed to get PCIe Device properties ec: "
                            << ec.value() << ": " << ec.message();
                        if (ec.value() ==
                            boost::system::linux_error::bad_request_descriptor)
                        {
                            messages::resourceNotFound(asyncResp->res,
                                                       "PCIeDevice", device);
                        }
                        else
                        {
                            messages::internalError(asyncResp->res);
                        }
                        return;
                    }

                    nlohmann::json& pcieFunctionList =
                        asyncResp->res.jsonValue["Members"];
                    pcieFunctionList = nlohmann::json::array();
                    static constexpr const int maxPciFunctionNum = 8;
                    for (int functionNum = 0; functionNum < maxPciFunctionNum;
                         functionNum++)
                    {
                        // Check if this function exists by looking for a device
                        // ID
                        std::string devIDProperty =
                            "Function" + std::to_string(functionNum) +
                            "DeviceId";
                        std::string* property = std::get_if<std::string>(
                            &pcieDevProperties[devIDProperty]);
                        if (property && !property->empty())
                        {
                            pcieFunctionList.push_back(
                                {{"@odata.id",
                                  "/redfish/v1/Systems/system/PCIeDevices/" +
                                      device + "/PCIeFunctions/" +
                                      std::to_string(functionNum)}});
                        }
                    }
                    asyncResp->res.jsonValue["PCIeFunctions@odata.count"] =
                        pcieFunctionList.size();
                };

                retrievePCIeDeviceList(asyncResp);
                auto it = mapOfPcieDevicePaths.find(device);
                if (it != mapOfPcieDevicePaths.end())
                {
                    std::string& serviceName = std::get<0>(it->second);
                    std::string& path = std::get<1>(it->second);
                    crow::connections::systemBus->async_method_call(
                        std::move(getPCIeDeviceCallback), serviceName, path,
                        "org.freedesktop.DBus.Properties", "GetAll",
                        std::string(pcieDeviceInterface));
                }
                else
                {
                    messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                               device);
                    return;
                }
            });
}

inline void requestRoutesSystemPCIeFunction(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/system/PCIeDevices/<str>/PCIeFunctions/<str>/")
        .privileges(redfish::privileges::getPCIeFunction)
        .methods(
            boost::beast::http::verb::get)([](const crow::Request&,
                                              const std::shared_ptr<
                                                  bmcweb::AsyncResp>& asyncResp,
                                              const std::string& device,
                                              const std::string& function) {
            auto getPCIeDeviceCallback = [asyncResp, device, function](
                                             const boost::system::error_code ec,
                                             boost::container::flat_map<
                                                 std::string,
                                                 std::variant<std::string>>&
                                                 pcieDevProperties) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG
                        << "failed to get PCIe Device properties ec: "
                        << ec.value() << ": " << ec.message();
                    if (ec.value() ==
                        boost::system::linux_error::bad_request_descriptor)
                    {
                        messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                                   device);
                    }
                    else
                    {
                        messages::internalError(asyncResp->res);
                    }
                    return;
                }

                // Check if this function exists by looking for a device ID
                std::string devIDProperty = "Function" + function + "DeviceId";
                if (std::string* property = std::get_if<std::string>(
                        &pcieDevProperties[devIDProperty]);
                    property && property->empty())
                {
                    messages::resourceNotFound(asyncResp->res, "PCIeFunction",
                                               function);
                    return;
                }

                asyncResp->res.jsonValue = {
                    {"@odata.type", "#PCIeFunction.v1_2_0.PCIeFunction"},
                    {"@odata.id", "/redfish/v1/Systems/system/PCIeDevices/" +
                                      device + "/PCIeFunctions/" + function},
                    {"Name", "PCIe Function"},
                    {"Id", function},
                    {"FunctionId", std::stoi(function)},
                    {"Links",
                     {{"PCIeDevice",
                       {{"@odata.id",
                         "/redfish/v1/Systems/system/PCIeDevices/" +
                             device}}}}}};

                if (std::string* property = std::get_if<std::string>(
                        &pcieDevProperties["Function" + function + "DeviceId"]);
                    property)
                {
                    asyncResp->res.jsonValue["DeviceId"] = *property;
                }

                if (std::string* property = std::get_if<std::string>(
                        &pcieDevProperties["Function" + function + "VendorId"]);
                    property)
                {
                    asyncResp->res.jsonValue["VendorId"] = *property;
                }

                if (std::string* property = std::get_if<std::string>(
                        &pcieDevProperties["Function" + function +
                                           "FunctionType"]);
                    property)
                {
                    if (!property->empty())
                    {
                        asyncResp->res.jsonValue["FunctionType"] = *property;
                    }
                }

                if (std::string* property = std::get_if<std::string>(
                        &pcieDevProperties["Function" + function +
                                           "DeviceClass"]);
                    property)
                {
                    if (!property->empty())
                    {
                        asyncResp->res.jsonValue["DeviceClass"] = *property;
                    }
                }

                if (std::string* property = std::get_if<std::string>(
                        &pcieDevProperties["Function" + function +
                                           "ClassCode"]);
                    property)
                {
                    asyncResp->res.jsonValue["ClassCode"] = *property;
                }

                if (std::string* property = std::get_if<std::string>(
                        &pcieDevProperties["Function" + function +
                                           "RevisionId"]);
                    property)
                {
                    asyncResp->res.jsonValue["RevisionId"] = *property;
                }

                if (std::string* property = std::get_if<std::string>(
                        &pcieDevProperties["Function" + function +
                                           "SubsystemId"]);
                    property)
                {
                    asyncResp->res.jsonValue["SubsystemId"] = *property;
                }

                if (std::string* property = std::get_if<std::string>(
                        &pcieDevProperties["Function" + function +
                                           "SubsystemVendorId"]);
                    property)
                {
                    asyncResp->res.jsonValue["SubsystemVendorId"] = *property;
                }
            };

            retrievePCIeDeviceList(asyncResp);
            auto it = mapOfPcieDevicePaths.find(device);
            if (it != mapOfPcieDevicePaths.end())
            {
                std::string& serviceName = std::get<0>(it->second);
                std::string& path = std::get<1>(it->second);
                crow::connections::systemBus->async_method_call(
                    std::move(getPCIeDeviceCallback), serviceName, path,
                    "org.freedesktop.DBus.Properties", "GetAll",
                    std::string(pcieDeviceInterface));
            }
            else
            {
                messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                           device);
                return;
            }
        });
}

} // namespace redfish
