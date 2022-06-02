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

#include "generated/enums/pcie_device.hpp"

#include <app.hpp>
#include <boost/system/linux_error.hpp>
#include <dbus_utility.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/pcie_util.hpp>

#include <map>
#include <string>
#include <vector>

namespace redfish
{

static constexpr char const* pcieDeviceInterface =
    "xyz.openbmc_project.Inventory.Item.PCIeDevice";
static constexpr char const* pcieSlotInterface =
    "xyz.openbmc_project.Inventory.Item.PCIeSlot";

using PCIeDevice = std::string;
using PCIeDevicePath = std::string;
using ServiceName = std::string;
// will be used when we need to fetch single PCIeDevice properties.

using PCIeDeviceNameToPathMap =
    std::map<PCIeDevice, std::pair<ServiceName, PCIeDevicePath>>;

PCIeDeviceNameToPathMap savedMapOfPcieDevicePaths;

/**
 * @brief Get device name to path map
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       callback        Callback method.
 */
template <typename Callback>
static inline void getMapOfPCIeDeviceToPaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Callback&& callback)
{
    if (!savedMapOfPcieDevicePaths.empty())
    {
        // Shortcut if mapofPcieDevicePath is already built
        const boost::system::error_code ec;
        callback(ec, savedMapOfPcieDevicePaths);
        return;
    }

    // build mapOfPcieDevicePath
    constexpr std::array<std::string_view, 1> interfaces = {
        pcieDeviceInterface};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, callback{std::forward<Callback>(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subTree) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "no PCIe device paths found ec: "
                             << ec.message();
            // Not an error, system just doesn't have PCIe info
            callback(ec, savedMapOfPcieDevicePaths);
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

                savedMapOfPcieDevicePaths.emplace(
                    devName, std::make_pair(serviceName, pcieDevicePath));
            }
        }
        callback(ec, savedMapOfPcieDevicePaths);
        });
}

static inline void
    getPCIeDeviceList(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& name)
{
    getMapOfPCIeDeviceToPaths(
        asyncResp,
        [asyncResp, name](const boost::system::error_code&,
                          const PCIeDeviceNameToPathMap& mapOfPcieDevicePaths) {
        nlohmann::json& pcieDeviceList = asyncResp->res.jsonValue[name];
        pcieDeviceList = nlohmann::json::array();

        for (const auto& [devName, serviceNameAndPaths] : mapOfPcieDevicePaths)
        {
            nlohmann::json::object_t pcieDevice;
            pcieDevice["@odata.id"] = crow::utility::urlFromPieces(
                "redfish", "v1", "Systems", "system", "PCIeDevices", devName);
            pcieDeviceList.push_back(std::move(pcieDevice));
        }
        asyncResp->res.jsonValue[name + "@odata.count"] =
            mapOfPcieDevicePaths.size();
        });
}

/**
 * @brief Fill PCIeDevice Status and Health based on PCIeSlot Link Status
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
                            const bool* value = std::get_if<bool>(&propVariant);
                            if (value == nullptr)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            asyncResp->res
                                .jsonValue["Oem"]["IBM"]["LinkReset"] = *value;
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
    constexpr std::array<std::string_view, 1> interfaces = {pcieSlotInterface};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, pcieSlotPath, pcieDevice,
         callback{std::forward<Callback>(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subTree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "DBUS response error on GetSubTree"
                             << ec.message();
            messages::internalError(asyncResp->res);
            return;
        }

        if (subTree.empty())
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
        });
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

    getMapOfPCIeDeviceToPaths(
        asyncResp,
        [asyncResp, pcieDevice, callback{std::forward<Callback>(callback)}](
            const boost::system::error_code& ec,
            const PCIeDeviceNameToPathMap& mapOfPcieDevicePaths) {
        if (ec)
        {
            return;
        }
        auto it = mapOfPcieDevicePaths.find(pcieDevice);
        if (it != mapOfPcieDevicePaths.end())
        {
            const std::string& objectPath = std::get<1>(it->second);
            sdbusplus::message::object_path path(objectPath);
            std::string pcieSlotPath = path.parent_path();

            findPcieSlotServiceMap(asyncResp, pcieSlotPath, pcieDevice,
                                   std::move(callback));
        }
        });
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
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/PCIeDevices/")
        .privileges(redfish::privileges::getPCIeDeviceCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (systemName != "system")
        {
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }

        asyncResp->res.jsonValue["@odata.type"] =
            "#PCIeDeviceCollection.PCIeDeviceCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/system/PCIeDevices";
        asyncResp->res.jsonValue["Name"] = "PCIe Device Collection";
        asyncResp->res.jsonValue["Description"] = "Collection of PCIe Devices";
        asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
        asyncResp->res.jsonValue["Members@odata.count"] = 0;
        getPCIeDeviceList(asyncResp, "Members");
        });
}

inline std::optional<pcie_device::PCIeTypes>
    redfishPcieGenerationFromDbus(const std::string& generationInUse)
{
    if (generationInUse ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen1")
    {
        return pcie_device::PCIeTypes::Gen1;
    }
    if (generationInUse ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen2")
    {
        return pcie_device::PCIeTypes::Gen2;
    }
    if (generationInUse ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen3")
    {
        return pcie_device::PCIeTypes::Gen3;
    }
    if (generationInUse ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen4")
    {
        return pcie_device::PCIeTypes::Gen4;
    }
    if (generationInUse ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen5")
    {
        return pcie_device::PCIeTypes::Gen5;
    }
    if (generationInUse.empty() ||
        generationInUse ==
            "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Unknown")
    {
        return pcie_device::PCIeTypes::Invalid;
    }

    // The value is not unknown or Gen1-5, need return an internal error.
    return std::nullopt;
}
inline void getPCIeDevices(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& device)
{
    auto getPCIeDevicePropertiesCallback =
        [asyncResp, device](
            const dbus::utility::DBusPropertiesMap& pcieDevProperties,
            const std::string& /* device */, const std::string& /* path */) {
        asyncResp->res.jsonValue = {
            {"@odata.type", "#PCIeDevice.v1_9_0.PCIeDevice"},
            {"@odata.id", "/redfish/v1/Systems/system/PCIeDevices/" + device},
            {"Name", "PCIe Device"},
            {"Id", device}};

        const std::string* manufacturer = nullptr;
        const std::string* deviceType = nullptr;
        const std::string* generationInUse = nullptr;
        const std::string* partNumber = nullptr;
        const std::string* serialNumber = nullptr;
        const std::string* model = nullptr;
        const std::string* sparePartNumber = nullptr;
        const std::string* prettyName = nullptr;
        const std::string* locationCode = nullptr;
        const bool* present = nullptr;
        const int64_t* lanesInUse = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), pcieDevProperties, "Manufacturer",
            manufacturer, "DeviceType", deviceType, "GenerationInUse",
            generationInUse, "PartNumber", partNumber, "SerialNumber",
            serialNumber, "Model", model, "SparePartNumber", sparePartNumber,
            "Name", prettyName, "LocationCode", locationCode, "Present",
            present, "LanesInUse", lanesInUse);

        if (!success)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        if (generationInUse != nullptr)
        {
            std::optional<pcie_device::PCIeTypes> redfishGenerationInUse =
                redfishPcieGenerationFromDbus(*generationInUse);
            if (!redfishGenerationInUse)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            if (*redfishGenerationInUse != pcie_device::PCIeTypes::Invalid)
            {
                asyncResp->res.jsonValue["PCIeInterface"]["PCIeType"] =
                    *redfishGenerationInUse;
            }
        }

        if (manufacturer != nullptr)
        {
            asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;
        }

        if (deviceType != nullptr && !deviceType->empty())
        {
            asyncResp->res.jsonValue["DeviceType"] = *deviceType;
        }

        if (partNumber != nullptr)
        {
            asyncResp->res.jsonValue["PartNumber"] = *partNumber;
        }

        if (serialNumber != nullptr)
        {
            asyncResp->res.jsonValue["SerialNumber"] = *serialNumber;
        }

        if (model != nullptr)
        {
            asyncResp->res.jsonValue["Model"] = *model;
        }

        if (sparePartNumber != nullptr)
        {
            asyncResp->res.jsonValue["SparePartNumber"] = *sparePartNumber;
        }

        if (prettyName != nullptr)
        {
            asyncResp->res.jsonValue["Name"] = *prettyName;
        }

        if (locationCode != nullptr)
        {
            asyncResp->res
                .jsonValue["Slot"]["Location"]["PartLocation"]["ServiceLabel"] =
                *locationCode;
        }

        // The default value of LanesInUse is 0, and the field
        // will be left as off if it is a default value.
        if (lanesInUse != nullptr && *lanesInUse != 0)
        {
            asyncResp->res.jsonValue["PCIeInterface"]["LanesInUse"] =
                *lanesInUse;
        }

        // Link status
        addLinkStatusToPcieDevice(asyncResp, device);

        asyncResp->res.jsonValue["PCIeFunctions"]["@odata.id"] =
            crow::utility::urlFromPieces("redfish", "v1", "Systems", "system",
                                         "PCIeDevices", device,
                                         "PCIeFunctions");
    };

    getMapOfPCIeDeviceToPaths(
        asyncResp,
        [asyncResp, device, getPCIeDevicePropertiesCallback](
            const boost::system::error_code&,
            const PCIeDeviceNameToPathMap& mapOfPcieDevicePaths) {
        auto it = mapOfPcieDevicePaths.find(device);
        if (it == mapOfPcieDevicePaths.end())
        {
            messages::resourceNotFound(asyncResp->res, "PCIeDevice", device);
            return;
        }

        const std::string& serviceName = std::get<0>(it->second);
        const std::string& path = std::get<1>(it->second);
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, serviceName, path, "",
            [asyncResp, device, path, getPCIeDevicePropertiesCallback](
                const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& pcieDevProperties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG
                    << "failed to get PCIe Device properties ec: " << ec.value()
                    << ": " << ec.message();
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
            getPCIeDevicePropertiesCallback(pcieDevProperties, device, path);
            });
        });
}

inline void requestRoutesSystemPCIeDevice(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/PCIeDevices/<str>/")
        .privileges(redfish::privileges::getPCIeDevice)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName, const std::string& device) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (systemName != "system")
        {
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }
        getPCIeDevices(asyncResp, device);
        });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/PCIeDevices/<str>/")
        .privileges(redfish::privileges::patchPCIeDevice)
        .methods(boost::beast::http::verb::patch)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& device) {
        std::optional<std::vector<nlohmann::json>> slotsData;
        if (!json_util::readJsonPatch(req, asyncResp->res, "Oem", slotsData))
        {
            return;
        }
        if (!slotsData || slotsData->empty())
        {
            return;
        }

        std::vector<nlohmann::json> slots = std::move(*slotsData);
        for (auto& slot : slots)
        {
            std::optional<nlohmann::json> ibmOem;
            if (redfish::json_util::readJson(slot, asyncResp->res, "IBM",
                                             ibmOem))
            {
                if (ibmOem)
                {
                    std::optional<bool> linkReset;
                    if (json_util::readJson(*ibmOem, asyncResp->res,
                                            "LinkReset", linkReset))
                    {
                        pcieSlotLinkReset(asyncResp, device, *linkReset);
                    }
                }
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
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& device) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        asyncResp->res.jsonValue["@odata.type"] =
            "#PCIeFunctionCollection.PCIeFunctionCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/system/PCIeDevices/" + device +
            "/PCIeFunctions";
        asyncResp->res.jsonValue["Name"] = "PCIe Function Collection";
        asyncResp->res.jsonValue["Description"] =
            "Collection of PCIe Functions for PCIe Device " + device;

        auto getPCIeDeviceCallback =
            [asyncResp, device](
                const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& pcieDevProperties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG
                    << "failed to get PCIe Device properties ec: " << ec.value()
                    << ": " << ec.message();
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

            nlohmann::json& pcieFunctionList =
                asyncResp->res.jsonValue["Members"];
            pcieFunctionList = nlohmann::json::array();
            static constexpr const int maxPciFunctionNum = 8;

            for (int functionNum = 0; functionNum < maxPciFunctionNum;
                 functionNum++)
            {
                // Check if this function exists by
                // looking for a device ID
                std::string devIDProperty =
                    "Function" + std::to_string(functionNum) + "DeviceId";
                const std::string* property = nullptr;
                for (const auto& propEntry : pcieDevProperties)
                {
                    if (propEntry.first == devIDProperty)
                    {
                        property = std::get_if<std::string>(&propEntry.second);
                    }
                }
                if (property == nullptr || property->empty())
                {
                    continue;
                }
                nlohmann::json::object_t pcieFunction;
                pcieFunction["@odata.id"] = crow::utility::urlFromPieces(
                    "redfish", "v1", "Systems", "system", "PCIeDevices", device,
                    "PCIeFunctions", std::to_string(functionNum));
                pcieFunctionList.push_back(std::move(pcieFunction));
            }
            asyncResp->res.jsonValue["PCIeFunctions@odata.count"] =
                pcieFunctionList.size();
        };

        getMapOfPCIeDeviceToPaths(
            asyncResp,
            [asyncResp, device, getPCIeDeviceCallback](
                const boost::system::error_code&,
                const PCIeDeviceNameToPathMap& mapOfPcieDevicePaths) {
            auto it = mapOfPcieDevicePaths.find(device);
            if (it != mapOfPcieDevicePaths.end())
            {
                const std::string& serviceName = std::get<0>(it->second);
                const std::string& path = std::get<1>(it->second);
                sdbusplus::asio::getAllProperties(
                    *crow::connections::systemBus, serviceName, path,
                    pcieDeviceInterface, std::move(getPCIeDeviceCallback));
            }
            else
            {
                messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                           device);
                return;
            }
            });
        });
} // namespace redfish

inline void requestRoutesSystemPCIeFunction(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/system/PCIeDevices/<str>/PCIeFunctions/<str>/")
        .privileges(redfish::privileges::getPCIeFunction)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& device, const std::string& function) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        auto getPCIeDeviceCallback =
            [asyncResp, device,
             function](const boost::system::error_code ec,
                       boost::container::flat_map<std::string,
                                                  std::variant<std::string>>&
                           pcieDevProperties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG
                    << "failed to get PCIe Device properties ec: " << ec.value()
                    << ": " << ec.message();
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

            // Check if this function exists by
            // looking for a device ID
            std::string functionName = "Function" + function;
            std::string devIDProperty = functionName + "DeviceId";

            const std::string* devIdProperty = nullptr;
            for (const auto& property : pcieDevProperties)
            {
                if (property.first == devIDProperty)
                {
                    devIdProperty = std::get_if<std::string>(&property.second);
                    continue;
                }
            }
            if (devIdProperty == nullptr || devIdProperty->empty())
            {
                messages::resourceNotFound(asyncResp->res, "PCIeFunction",
                                           function);
                return;
            }

            asyncResp->res.jsonValue["@odata.type"] =
                "#PCIeFunction.v1_2_3.PCIeFunction";
            asyncResp->res.jsonValue["@odata.id"] =
                crow::utility::urlFromPieces("redfish", "v1", "Systems",
                                             "system", "PCIeDevices", device,
                                             "PCIeFunctions", function);
            asyncResp->res.jsonValue["Name"] = "PCIe Function";
            asyncResp->res.jsonValue["Id"] = function;
            asyncResp->res.jsonValue["FunctionId"] = std::stoi(function);
            asyncResp->res.jsonValue["Links"]["PCIeDevice"]["@odata.id"] =
                crow::utility::urlFromPieces("redfish", "v1", "Systems",
                                             "system", "PCIeDevices", device);

            for (const auto& property : pcieDevProperties)
            {
                const std::string* strProperty =
                    std::get_if<std::string>(&property.second);

                if (property.first == functionName + "DeviceId")
                {
                    asyncResp->res.jsonValue["DeviceId"] = *strProperty;
                }
                if (property.first == functionName + "VendorId")
                {
                    asyncResp->res.jsonValue["VendorId"] = *strProperty;
                }
                if (property.first == functionName + "FunctionType")
                {
                    if (!strProperty->empty())
                    {
                        asyncResp->res.jsonValue["FunctionType"] = *strProperty;
                    }
                }
                if (property.first == functionName + "DeviceClass")
                {
                    if (!strProperty->empty())
                    {
                        asyncResp->res.jsonValue["DeviceClass"] = *strProperty;
                    }
                }
                if (property.first == functionName + "ClassCode")
                {
                    asyncResp->res.jsonValue["ClassCode"] = *strProperty;
                }
                if (property.first == functionName + "RevisionId")
                {
                    asyncResp->res.jsonValue["RevisionId"] = *strProperty;
                }
                if (property.first == functionName + "SubsystemId")
                {
                    asyncResp->res.jsonValue["SubsystemId"] = *strProperty;
                }
                if (property.first == functionName + "SubsystemVendorId")
                {
                    asyncResp->res.jsonValue["SubsystemVendorId"] =
                        *strProperty;
                }
            }
        };

        getMapOfPCIeDeviceToPaths(
            asyncResp,
            [asyncResp, device, getPCIeDeviceCallback](
                const boost::system::error_code&,
                const PCIeDeviceNameToPathMap& mapOfPcieDevicePaths) {
            auto it = mapOfPcieDevicePaths.find(device);
            if (it != mapOfPcieDevicePaths.end())
            {
                const std::string& serviceName = std::get<0>(it->second);
                const std::string& path = std::get<1>(it->second);
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
        });
}

} // namespace redfish
