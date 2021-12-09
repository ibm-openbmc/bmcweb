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

#include <map>

namespace redfish
{

static constexpr char const* pcieRootPath = "/xyz/openbmc_project/inventory";
static constexpr char const* pcieDeviceInterface =
    "xyz.openbmc_project.Inventory.Item.PCIeDevice";

using PCIeDevice = std::string;
using PCIeDevicePath = std::string;
using ServiceName = std::string;
using MapperGetSubTreeResponse = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;

// will be used when we need to fetch single PCIeDevice properties.
std::map<PCIeDevice, std::pair<ServiceName, PCIeDevicePath>>
    mapOfPcieDevicePaths;

static inline void
    getPCIeDeviceList(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& name)
{
    auto getPCIeMapCallback = [asyncResp,
                               name](const boost::system::error_code ec,
                                     const MapperGetSubTreeResponse& subTree) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "no PCIe device paths found ec: "
                             << ec.message();
            // Not an error, system just doesn't have PCIe info
            return;
        }

        nlohmann::json& pcieDeviceList = asyncResp->res.jsonValue[name];
        pcieDeviceList = nlohmann::json::array();

        for (const auto& [pcieDevicePath, serviceMap] : subTree)
        {
            for (const auto& [serviceName, interfaceList] : serviceMap)
            {
                std::string devName =
                    sdbusplus::message::object_path(pcieDevicePath).filename();

                if (devName.empty())
                {
                    BMCWEB_LOG_DEBUG << "Invalid Name";
                    continue;
                }

                mapOfPcieDevicePaths.emplace(
                    devName, std::make_pair(serviceName, pcieDevicePath));
                pcieDeviceList.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Systems/system/PCIeDevices/" + devName}});
            }
        }

        asyncResp->res.jsonValue[name + "@odata.count"] = pcieDeviceList.size();
    };
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeMapCallback), "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        std::string(pcieRootPath), 0,
        std::array<const char*, 1>{pcieDeviceInterface});
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

                    asyncResp->res.jsonValue = {
                        {"@odata.type", "#PCIeDevice.v1_4_0.PCIeDevice"},
                        {"@odata.id",
                         "/redfish/v1/Systems/system/PCIeDevices/" + device},
                        {"Name", "PCIe Device"},
                        {"Id", device}};

                    if (std::string* property = std::get_if<std::string>(
                            &pcieDevProperties["Manufacturer"]);
                        property)
                    {
                        asyncResp->res.jsonValue["Manufacturer"] = *property;
                    }

                    if (std::string* property = std::get_if<std::string>(
                            &pcieDevProperties["DeviceType"]);
                        property)
                    {
                        asyncResp->res.jsonValue["DeviceType"] = *property;
                    }

                    if (std::string* property = std::get_if<std::string>(
                            &pcieDevProperties["GenerationInUse"]);
                        property)
                    {
                        std::string generation = analysisGeneration(*property);
                        if (!generation.empty())
                        {
                            asyncResp->res
                                .jsonValue["PCIeInterface"]["PCIeType"] =
                                generation;
                        }
                    }

                    asyncResp->res.jsonValue["PCIeFunctions"] = {
                        {"@odata.id",
                         "/redfish/v1/Systems/system/PCIeDevices/" + device +
                             "/PCIeFunctions"}};
                };

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
                    asyncResp->res.jsonValue["FunctionType"] = *property;
                }

                if (std::string* property = std::get_if<std::string>(
                        &pcieDevProperties["Function" + function +
                                           "DeviceClass"]);
                    property)
                {
                    asyncResp->res.jsonValue["DeviceClass"] = *property;
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
