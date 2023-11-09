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

#include "dbus_singleton.hpp"
#include "led.hpp"
#include "pcie.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "utils/systems_utils.hpp"
#include "utils/time_utils.hpp"
#ifdef BMCWEB_ENABLE_IBM_LED_EXTENSIONS
#include "oem/ibm/lamp_test.hpp"
#include "oem/ibm/system_attention_indicator.hpp"
#endif
#include "oem/ibm/pcie_topology_refresh.hpp"

#include <app.hpp>
#include <boost/asio/error.hpp>
#include <boost/container/flat_map.hpp>
#include <dbus_utility.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/sw_utils.hpp>

#include <variant>

namespace redfish
{

const static std::array<std::pair<std::string_view, std::string_view>, 2>
    protocolToDBusForSystems{
        {{"SSH", "obmc-console-ssh"}, {"IPMI", "phosphor-ipmi-net"}}};

/**
 * @brief Updates the Functional State of DIMMs
 *
 * @param[in] aResp Shared pointer for completing asynchronous calls
 * @param[in] dimmState Dimm's Functional state, true/false
 *
 * @return None.
 */
inline void
    updateDimmProperties(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         bool isDimmFunctional)
{
    BMCWEB_LOG_DEBUG << "Dimm Functional: " << isDimmFunctional;

    // Set it as Enabled if at least one DIMM is functional
    // Update STATE only if previous State was DISABLED and current Dimm is
    // ENABLED.
    const nlohmann::json& prevMemSummary =
        aResp->res.jsonValue["MemorySummary"]["Status"]["State"];
    if (prevMemSummary == "Disabled")
    {
        if (isDimmFunctional)
        {
            aResp->res.jsonValue["MemorySummary"]["Status"]["State"] =
                "Enabled";
        }
    }
}

/*
 * @brief Update "ProcessorSummary" "Count" based on Cpu PresenceState
 *
 * @param[in] aResp Shared pointer for completing asynchronous calls
 * @param[in] cpuPresenceState CPU present or not
 *
 * @return None.
 */
inline void
    modifyCpuPresenceState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           bool isCpuPresent)
{
    BMCWEB_LOG_DEBUG << "Cpu Present: " << isCpuPresent;

    if (isCpuPresent)
    {
        nlohmann::json& procCount =
            aResp->res.jsonValue["ProcessorSummary"]["Count"];
        auto* procCountPtr =
            procCount.get_ptr<nlohmann::json::number_integer_t*>();
        if (procCountPtr != nullptr)
        {
            // shouldn't be possible to be nullptr
            *procCountPtr += 1;
        }
    }
}

/*
 * @brief Update "ProcessorSummary" "Status" "State" based on
 *        CPU Functional State
 *
 * @param[in] aResp Shared pointer for completing asynchronous calls
 * @param[in] cpuFunctionalState is CPU functional true/false
 *
 * @return None.
 */
inline void
    modifyCpuFunctionalState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             bool isCpuFunctional)
{
    BMCWEB_LOG_DEBUG << "Cpu Functional: " << isCpuFunctional;

    const nlohmann::json& prevProcState =
        aResp->res.jsonValue["ProcessorSummary"]["Status"]["State"];

    // Set it as Enabled if at least one CPU is functional
    // Update STATE only if previous State was Non_Functional and current CPU is
    // Functional.
    if (prevProcState == "Disabled")
    {
        if (isCpuFunctional)
        {
            aResp->res.jsonValue["ProcessorSummary"]["Status"]["State"] =
                "Enabled";
        }
    }
}

inline void getProcessorProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>&
        properties)
{
    BMCWEB_LOG_DEBUG << "Got " << properties.size() << " Cpu properties.";

    // TODO: Get Model

    const uint16_t* coreCount = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "CoreCount", coreCount);

    if (!success)
    {
        messages::internalError(aResp->res);
        return;
    }

    if (coreCount != nullptr)
    {
        nlohmann::json& coreCountJson =
            aResp->res.jsonValue["ProcessorSummary"]["CoreCount"];
        uint64_t* coreCountJsonPtr = coreCountJson.get_ptr<uint64_t*>();

        if (coreCountJsonPtr == nullptr)
        {
            coreCountJson = *coreCount;
        }
        else
        {
            *coreCountJsonPtr += *coreCount;
        }
    }
}

/*
 * @brief Get ProcessorSummary fields
 *
 * @param[in] aResp Shared pointer for completing asynchronous calls
 * @param[in] service dbus service for Cpu Information
 * @param[in] path dbus path for Cpu
 *
 * @return None.
 */
inline void getProcessorSummary(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& service,
                                const std::string& path)
{
    auto getCpuPresenceState = [aResp](const boost::system::error_code ec3,
                                       const bool cpuPresenceCheck) {
        if (ec3)
        {
            BMCWEB_LOG_ERROR << "DBUS response error " << ec3;
            return;
        }
        modifyCpuPresenceState(aResp, cpuPresenceCheck);
    };

    auto getCpuFunctionalState = [aResp](const boost::system::error_code ec3,
                                         const bool cpuFunctionalCheck) {
        if (ec3)
        {
            BMCWEB_LOG_ERROR << "DBUS response error " << ec3;
            return;
        }
        modifyCpuFunctionalState(aResp, cpuFunctionalCheck);
    };

    // Get the Presence of CPU
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Inventory.Item", "Present",
        std::move(getCpuPresenceState));

    // Get the Functional State
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.State.Decorator.OperationalStatus", "Functional",
        std::move(getCpuFunctionalState));

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Inventory.Item.Cpu",
        [aResp, service,
         path](const boost::system::error_code ec2,
               const dbus::utility::DBusPropertiesMap& properties) {
        if (ec2)
        {
            BMCWEB_LOG_ERROR << "DBUS response error " << ec2;
            messages::internalError(aResp->res);
            return;
        }
        getProcessorProperties(aResp, properties);
    });
}

/*
 * @brief Retrieves computer system properties over dbus
 *
 * @param[in] aResp Shared pointer for completing asynchronous calls
 * @param[in] systemHealth  Shared HealthPopulate pointer
 *
 * @return None.
 */
inline void getComputerSystem(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get available system components.";

    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "D-Bus response error: " << ec;
            messages::internalError(aResp->res);
            return;
        }
        // Iterate over all retrieved ObjectPaths.
        for (const std::pair<
                 std::string,
                 std::vector<std::pair<std::string, std::vector<std::string>>>>&
                 object : subtree)
        {
            const std::string& path = object.first;
            BMCWEB_LOG_DEBUG << "Got path: " << path;
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                connectionNames = object.second;
            if (connectionNames.empty())
            {
                continue;
            }

            // This is not system, so check if it's cpu, dimm, UUID or
            // BiosVer
            for (const auto& connection : connectionNames)
            {
                for (const auto& interfaceName : connection.second)
                {
                    if (interfaceName ==
                        "xyz.openbmc_project.Inventory.Item.Dimm")
                    {
                        BMCWEB_LOG_DEBUG
                            << "Found Dimm, now get its properties.";

                        sdbusplus::asio::getAllProperties(
                            *crow::connections::systemBus, connection.first,
                            path, "xyz.openbmc_project.Inventory.Item.Dimm",
                            [aResp, service{connection.first},
                             path](const boost::system::error_code ec2,
                                   const dbus::utility::DBusPropertiesMap&
                                       properties) {
                            if (ec2)
                            {
                                BMCWEB_LOG_ERROR << "DBUS response error "
                                                 << ec2;
                                messages::internalError(aResp->res);
                                return;
                            }
                            BMCWEB_LOG_DEBUG << "Got " << properties.size()
                                             << " Dimm properties.";

                            if (properties.empty())
                            {
                                sdbusplus::asio::getProperty<bool>(
                                    *crow::connections::systemBus, service,
                                    path,
                                    "xyz.openbmc_project.State."
                                    "Decorator.OperationalStatus",
                                    "Functional",
                                    [aResp](const boost::system::error_code ec3,
                                            bool dimmState) {
                                    if (ec3)
                                    {
                                        BMCWEB_LOG_ERROR
                                            << "DBUS response error " << ec3;
                                        return;
                                    }
                                    updateDimmProperties(aResp, dimmState);
                                });
                                return;
                            }

                            const uint32_t* memorySizeInKB = nullptr;

                            const bool success =
                                sdbusplus::unpackPropertiesNoThrow(
                                    dbus_utils::UnpackErrorPrinter(),
                                    properties, "MemorySizeInKB",
                                    memorySizeInKB);

                            if (!success)
                            {
                                messages::internalError(aResp->res);
                                return;
                            }

                            if (memorySizeInKB != nullptr)
                            {
                                nlohmann::json& totalMemory =
                                    aResp->res
                                        .jsonValue["MemorySummary"]
                                                  ["TotalSystemMemoryGiB"];
                                const double* preValue =
                                    totalMemory.get_ptr<const double*>();
                                if (preValue == nullptr)
                                {
                                    aResp->res
                                        .jsonValue["MemorySummary"]
                                                  ["TotalSystemMemoryGiB"] =
                                        static_cast<double>(*memorySizeInKB) /
                                        (1024 * 1024);
                                }
                                else
                                {
                                    aResp->res
                                        .jsonValue["MemorySummary"]
                                                  ["TotalSystemMemoryGiB"] =
                                        static_cast<double>(*memorySizeInKB) /
                                            (1024 * 1024) +
                                        *preValue;
                                }
                                aResp->res.jsonValue["MemorySummary"]["Status"]
                                                    ["State"] = "Enabled";
                            }
                        });
                    }
                    else if (interfaceName ==
                             "xyz.openbmc_project.Inventory.Item.Cpu")
                    {
                        BMCWEB_LOG_DEBUG
                            << "Found Cpu, now get its properties.";

                        getProcessorSummary(aResp, connection.first, path);
                    }
                    else if (interfaceName == "xyz.openbmc_project.Common.UUID")
                    {
                        BMCWEB_LOG_DEBUG
                            << "Found UUID, now get its properties.";

                        sdbusplus::asio::getAllProperties(
                            *crow::connections::systemBus, connection.first,
                            path, "xyz.openbmc_project.Common.UUID",
                            [aResp](const boost::system::error_code ec3,
                                    const dbus::utility::DBusPropertiesMap&
                                        properties) {
                            if (ec3)
                            {
                                BMCWEB_LOG_ERROR << "DBUS response error "
                                                 << ec3;
                                messages::internalError(aResp->res);
                                return;
                            }
                            BMCWEB_LOG_DEBUG << "Got " << properties.size()
                                             << " UUID properties.";

                            const std::string* uUID = nullptr;

                            const bool success =
                                sdbusplus::unpackPropertiesNoThrow(
                                    dbus_utils::UnpackErrorPrinter(),
                                    properties, "UUID", uUID);

                            if (!success)
                            {
                                messages::internalError(aResp->res);
                                return;
                            }

                            if (uUID != nullptr)
                            {
                                std::string valueStr = *uUID;
                                if (valueStr.size() == 32)
                                {
                                    valueStr.insert(8, 1, '-');
                                    valueStr.insert(13, 1, '-');
                                    valueStr.insert(18, 1, '-');
                                    valueStr.insert(23, 1, '-');
                                }
                                BMCWEB_LOG_DEBUG << "UUID = " << valueStr;
                                aResp->res.jsonValue["UUID"] = valueStr;
                            }
                        });
                    }
                    else if (interfaceName ==
                             "xyz.openbmc_project.Inventory.Item.System")
                    {
                        sdbusplus::asio::getAllProperties(
                            *crow::connections::systemBus, connection.first,
                            path,
                            "xyz.openbmc_project.Inventory.Decorator.Asset",
                            [aResp](const boost::system::error_code ec2,
                                    const dbus::utility::DBusPropertiesMap&
                                        propertiesList) {
                            if (ec2)
                            {
                                // doesn't have to include this
                                // interface
                                return;
                            }
                            BMCWEB_LOG_DEBUG << "Got " << propertiesList.size()
                                             << " properties for system";

                            const std::string* partNumber = nullptr;
                            const std::string* serialNumber = nullptr;
                            const std::string* manufacturer = nullptr;
                            const std::string* model = nullptr;
                            const std::string* subModel = nullptr;

                            const bool success =
                                sdbusplus::unpackPropertiesNoThrow(
                                    dbus_utils::UnpackErrorPrinter(),
                                    propertiesList, "PartNumber", partNumber,
                                    "SerialNumber", serialNumber,
                                    "Manufacturer", manufacturer, "Model",
                                    model, "SubModel", subModel);

                            if (!success)
                            {
                                messages::internalError(aResp->res);
                                return;
                            }

                            if (partNumber != nullptr)
                            {
                                aResp->res.jsonValue["PartNumber"] =
                                    *partNumber;
                            }

                            if (serialNumber != nullptr)
                            {
                                aResp->res.jsonValue["SerialNumber"] =
                                    *serialNumber;
                            }

                            if (manufacturer != nullptr)
                            {
                                aResp->res.jsonValue["Manufacturer"] =
                                    *manufacturer;
                            }

                            if (model != nullptr)
                            {
                                aResp->res.jsonValue["Model"] = *model;
                            }

                            if (subModel != nullptr)
                            {
                                aResp->res.jsonValue["SubModel"] = *subModel;
                            }

                            // Grab the bios version
                            sw_util::populateSoftwareInformation(
                                aResp, sw_util::biosPurpose, "BiosVersion",
                                false);
                        });

                        sdbusplus::asio::getProperty<std::string>(
                            *crow::connections::systemBus, connection.first,
                            path,
                            "xyz.openbmc_project.Inventory.Decorator."
                            "AssetTag",
                            "AssetTag",
                            [aResp](const boost::system::error_code ec2,
                                    const std::string& value) {
                            if (ec2)
                            {
                                // doesn't have to include this
                                // interface
                                return;
                            }

                            aResp->res.jsonValue["AssetTag"] = value;
                        });
                    }
                }
                break;
            }
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", int32_t(0),
        std::array<const char*, 5>{
            "xyz.openbmc_project.Inventory.Decorator.Asset",
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Item.Dimm",
            "xyz.openbmc_project.Inventory.Item.System",
            "xyz.openbmc_project.Common.UUID",
        });
}

/**
 * @brief Retrieves host state properties over dbus
 *
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void getHostState(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get host information.";
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, "xyz.openbmc_project.State.Host",
        "/xyz/openbmc_project/state/host0", "xyz.openbmc_project.State.Host",
        "CurrentHostState",
        [aResp](const boost::system::error_code ec,
                const std::string& hostState) {
        if (ec)
        {
            if (ec == boost::system::errc::host_unreachable)
            {
                // Service not available, no error, just don't return
                // host state info
                BMCWEB_LOG_DEBUG << "Service not available " << ec;
                return;
            }
            BMCWEB_LOG_ERROR << "DBUS response error " << ec;
            messages::internalError(aResp->res);
            return;
        }

        BMCWEB_LOG_DEBUG << "Host state: " << hostState;
        // Verify Host State
        if (hostState == "xyz.openbmc_project.State.Host.HostState.Running")
        {
            aResp->res.jsonValue["PowerState"] = "On";
            aResp->res.jsonValue["Status"]["State"] = "Enabled";
        }
        else if (hostState ==
                 "xyz.openbmc_project.State.Host.HostState.Quiesced")
        {
            aResp->res.jsonValue["PowerState"] = "On";
            aResp->res.jsonValue["Status"]["State"] = "Quiesced";
        }
        else if (hostState ==
                 "xyz.openbmc_project.State.Host.HostState.DiagnosticMode")
        {
            aResp->res.jsonValue["PowerState"] = "On";
            aResp->res.jsonValue["Status"]["State"] = "InTest";
        }
        else if (
            hostState ==
            "xyz.openbmc_project.State.Host.HostState.TransitioningToRunning")
        {
            aResp->res.jsonValue["PowerState"] = "PoweringOn";
            aResp->res.jsonValue["Status"]["State"] = "Starting";
        }
        else if (hostState ==
                 "xyz.openbmc_project.State.Host.HostState.TransitioningToOff")
        {
            aResp->res.jsonValue["PowerState"] = "PoweringOff";
            aResp->res.jsonValue["Status"]["State"] = "Disabled";
        }
        else
        {
            aResp->res.jsonValue["PowerState"] = "Off";
            aResp->res.jsonValue["Status"]["State"] = "Disabled";
        }
    });
}

/**
 * @brief Translates boot type DBUS property value to redfish.
 *
 * @param[in] dbusType    The boot type in DBUS speak.
 *
 * @return Returns as a string, the boot type in Redfish terms. If translation
 * cannot be done, returns an empty string.
 */
inline std::string dbusToRfBootType(const std::string& dbusType)
{
    if (dbusType == "xyz.openbmc_project.Control.Boot.Type.Types.Legacy")
    {
        return "Legacy";
    }
    if (dbusType == "xyz.openbmc_project.Control.Boot.Type.Types.EFI")
    {
        return "UEFI";
    }
    return "";
}

/**
 * @brief Translates boot progress DBUS property value to redfish.
 *
 * @param[in] dbusBootProgress    The boot progress in DBUS speak.
 *
 * @return Returns as a string, the boot progress in Redfish terms. If
 *         translation cannot be done, returns "None".
 */
inline std::string dbusToRfBootProgress(const std::string& dbusBootProgress)
{
    // Now convert the D-Bus BootProgress to the appropriate Redfish
    // enum
    std::string rfBpLastState = "None";
    if (dbusBootProgress == "xyz.openbmc_project.State.Boot.Progress."
                            "ProgressStages.Unspecified")
    {
        rfBpLastState = "None";
    }
    else if (dbusBootProgress ==
             "xyz.openbmc_project.State.Boot.Progress.ProgressStages."
             "PrimaryProcInit")
    {
        rfBpLastState = "PrimaryProcessorInitializationStarted";
    }
    else if (dbusBootProgress ==
             "xyz.openbmc_project.State.Boot.Progress.ProgressStages."
             "BusInit")
    {
        rfBpLastState = "BusInitializationStarted";
    }
    else if (dbusBootProgress ==
             "xyz.openbmc_project.State.Boot.Progress.ProgressStages."
             "MemoryInit")
    {
        rfBpLastState = "MemoryInitializationStarted";
    }
    else if (dbusBootProgress ==
             "xyz.openbmc_project.State.Boot.Progress.ProgressStages."
             "SecondaryProcInit")
    {
        rfBpLastState = "SecondaryProcessorInitializationStarted";
    }
    else if (dbusBootProgress ==
             "xyz.openbmc_project.State.Boot.Progress.ProgressStages."
             "PCIInit")
    {
        rfBpLastState = "PCIResourceConfigStarted";
    }
    else if (dbusBootProgress ==
             "xyz.openbmc_project.State.Boot.Progress.ProgressStages."
             "SystemSetup")
    {
        rfBpLastState = "SetupEntered";
    }
    else if (dbusBootProgress ==
             "xyz.openbmc_project.State.Boot.Progress.ProgressStages."
             "SystemInitComplete")
    {
        rfBpLastState = "SystemHardwareInitializationComplete";
    }
    else if (dbusBootProgress ==
             "xyz.openbmc_project.State.Boot.Progress.ProgressStages."
             "OSStart")
    {
        rfBpLastState = "OSBootStarted";
    }
    else if (dbusBootProgress ==
             "xyz.openbmc_project.State.Boot.Progress.ProgressStages."
             "OSRunning")
    {
        rfBpLastState = "OSRunning";
    }
    else
    {
        BMCWEB_LOG_DEBUG << "Unsupported D-Bus BootProgress "
                         << dbusBootProgress;
        // Just return the default
    }
    return rfBpLastState;
}

/**
 * @brief Retrieves boot progress of the system
 *
 * @param[in] aResp  Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getBootProgress(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, "xyz.openbmc_project.State.Host",
        "/xyz/openbmc_project/state/host0",
        "xyz.openbmc_project.State.Boot.Progress", "BootProgress",
        [aResp](const boost::system::error_code ec,
                const std::string& bootProgressStr) {
        if (ec)
        {
            // BootProgress is an optional object so just do nothing if
            // not found
            return;
        }

        BMCWEB_LOG_DEBUG << "Boot Progress: " << bootProgressStr;

        aResp->res.jsonValue["BootProgress"]["LastState"] =
            dbusToRfBootProgress(bootProgressStr);
    });
}

/**
 * @brief Retrieves boot progress Last Update of the system
 *
 * @param[in] aResp  Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getBootProgressLastStateTime(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    sdbusplus::asio::getProperty<uint64_t>(
        *crow::connections::systemBus, "xyz.openbmc_project.State.Host",
        "/xyz/openbmc_project/state/host0",
        "xyz.openbmc_project.State.Boot.Progress", "BootProgressLastUpdate",
        [aResp](const boost::system::error_code ec,
                const uint64_t lastStateTime) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "D-BUS response error " << ec;
            return;
        }

        // BootProgressLastUpdate is the last time the BootProgress property
        // was updated. The time is the Epoch time, number of microseconds
        // since 1 Jan 1970 00::00::00 UTC."
        // https://github.com/openbmc/phosphor-dbus-interfaces/blob/master/
        // yaml/xyz/openbmc_project/State/Boot/Progress.interface.yaml#L11

        // Convert to ISO 8601 standard
        aResp->res.jsonValue["BootProgress"]["LastStateTime"] =
            redfish::time_utils::getDateTimeUintUs(lastStateTime);
    });
}

/**
 * @brief Retrieves the Last Reset Time
 *
 * "Reset" is an overloaded term in Redfish, "Reset" includes power on
 * and power off. Even though this is the "system" Redfish object look at the
 * chassis D-Bus interface for the LastStateChangeTime since this has the
 * last power operation time.
 *
 * @param[in] aResp     Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getLastResetTime(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Getting System Last Reset Time";

    sdbusplus::asio::getProperty<uint64_t>(
        *crow::connections::systemBus, "xyz.openbmc_project.State.Chassis",
        "/xyz/openbmc_project/state/chassis0",
        "xyz.openbmc_project.State.Chassis", "LastStateChangeTime",
        [aResp](const boost::system::error_code ec, uint64_t lastResetTime) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "D-BUS response error " << ec;
            return;
        }

        // LastStateChangeTime is epoch time, in milliseconds
        // https://github.com/openbmc/phosphor-dbus-interfaces/blob/33e8e1dd64da53a66e888d33dc82001305cd0bf9/xyz/openbmc_project/State/Chassis.interface.yaml#L19
        uint64_t lastResetTimeStamp = lastResetTime / 1000;

        // Convert to ISO 8601 standard
        aResp->res.jsonValue["LastResetTime"] =
            redfish::time_utils::getDateTimeUint(lastResetTimeStamp);
    });
}

/**
 * @brief Retrieves Automatic Retry properties. Known on D-Bus as AutoReboot.
 *
 * @param[in] aResp     Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getAutomaticRetry(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get Automatic Retry policy";

    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/control/host0/auto_reboot",
        "xyz.openbmc_project.Control.Boot.RebootPolicy", "AutoReboot",
        [aResp](const boost::system::error_code ec, bool autoRebootEnabled) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "D-BUS response error " << ec;
            return;
        }

        BMCWEB_LOG_DEBUG << "Auto Reboot: " << autoRebootEnabled;
        if (autoRebootEnabled)
        {
            aResp->res.jsonValue["Boot"]["AutomaticRetryConfig"] =
                "RetryAttempts";
            // If AutomaticRetry (AutoReboot) is enabled see how many
            // attempts are left
            sdbusplus::asio::getProperty<uint32_t>(
                *crow::connections::systemBus, "xyz.openbmc_project.State.Host",
                "/xyz/openbmc_project/state/host0",
                "xyz.openbmc_project.Control.Boot.RebootAttempts",
                "AttemptsLeft",
                [aResp](const boost::system::error_code ec2,
                        const uint32_t autoRebootAttemptsLeft) {
                if (ec2)
                {
                    BMCWEB_LOG_DEBUG << "D-BUS response error " << ec2;
                    return;
                }

                BMCWEB_LOG_DEBUG << "Auto Reboot Attempts Left: "
                                 << autoRebootAttemptsLeft;

                aResp->res
                    .jsonValue["Boot"]["RemainingAutomaticRetryAttempts"] =
                    autoRebootAttemptsLeft;
            });
        }
        else
        {
            aResp->res.jsonValue["Boot"]["AutomaticRetryConfig"] = "Disabled";
        }

        // Not on D-Bus. Hardcoded here:
        // https://github.com/openbmc/phosphor-state-manager/blob/1dbbef42675e94fb1f78edb87d6b11380260535a/meson_options.txt#L71
        aResp->res.jsonValue["Boot"]["AutomaticRetryAttempts"] = 3;

        // "AutomaticRetryConfig" can be 3 values, Disabled, RetryAlways,
        // and RetryAttempts. OpenBMC only supports Disabled and
        // RetryAttempts.
        aResp->res.jsonValue["Boot"]
                            ["AutomaticRetryConfig@Redfish.AllowableValues"] = {
            "Disabled", "RetryAttempts"};
    });
}

/**
 * @brief Retrieves power restore policy over DBUS.
 *
 * @param[in] aResp     Shared pointer for generating response message.
 *
 * @return None.
 */
inline void
    getPowerRestorePolicy(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get power restore policy";

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/control/host0/power_restore_policy",
        "xyz.openbmc_project.Control.Power.RestorePolicy", "PowerRestorePolicy",
        [aResp](const boost::system::error_code ec, const std::string& policy) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
            return;
        }

        const boost::container::flat_map<std::string, std::string> policyMaps = {
            {"xyz.openbmc_project.Control.Power.RestorePolicy.Policy.AlwaysOn",
             "AlwaysOn"},
            {"xyz.openbmc_project.Control.Power.RestorePolicy.Policy.AlwaysOff",
             "AlwaysOff"},
            {"xyz.openbmc_project.Control.Power.RestorePolicy.Policy.Restore",
             "LastState"},
            // Return `AlwaysOff` when power restore policy set to "None"
            {"xyz.openbmc_project.Control.Power.RestorePolicy.Policy.None",
             "AlwaysOff"}};

        auto policyMapsIt = policyMaps.find(policy);
        if (policyMapsIt == policyMaps.end())
        {
            messages::internalError(aResp->res);
            return;
        }

        aResp->res.jsonValue["PowerRestorePolicy"] = policyMapsIt->second;
    });
}

/**
 * @brief Stop Boot On Fault over DBUS.
 *
 * @param[in] aResp     Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getStopBootOnFault(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get Stop Boot On Fault";

    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/logging/settings",
        "xyz.openbmc_project.Logging.Settings", "QuiesceOnHwError",
        [aResp](const boost::system::error_code& ec, bool value) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(aResp->res);
            }
            return;
        }

        if (value)
        {
            aResp->res.jsonValue["Boot"]["StopBootOnFault"] = "AnyFault";
        }
        else
        {
            aResp->res.jsonValue["Boot"]["StopBootOnFault"] = "Never";
        }
    });
}

/**
 * @brief Get TrustedModuleRequiredToBoot property. Determines whether or not
 * TPM is required for booting the host.
 *
 * @param[in] aResp     Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getTrustedModuleRequiredToBoot(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get TPM required to boot.";

    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error on TPM.Policy GetSubTree"
                             << ec;
            // This is an optional D-Bus object so just return if
            // error occurs
            return;
        }
        if (subtree.empty())
        {
            // As noted above, this is an optional interface so just return
            // if there is no instance found
            return;
        }

        /* When there is more than one TPMEnable object... */
        if (subtree.size() > 1)
        {
            BMCWEB_LOG_DEBUG
                << "DBUS response has more than 1 TPM Enable object:"
                << subtree.size();
            // Throw an internal Error and return
            messages::internalError(aResp->res);
            return;
        }

        // Make sure the Dbus response map has a service and objectPath
        // field
        if (subtree[0].first.empty() || subtree[0].second.size() != 1)
        {
            BMCWEB_LOG_DEBUG << "TPM.Policy mapper error!";
            messages::internalError(aResp->res);
            return;
        }

        const std::string& path = subtree[0].first;
        const std::string& serv = subtree[0].second.begin()->first;

        // Valid TPM Enable object found, now reading the current value
        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus, serv, path,
            "xyz.openbmc_project.Control.TPM.Policy", "TPMEnable",
            [aResp](const boost::system::error_code ec2, bool tpmRequired) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR << "D-BUS response error on TPM.Policy Get"
                                 << ec2;
                messages::internalError(aResp->res);
                return;
            }

            if (tpmRequired)
            {
                aResp->res.jsonValue["Boot"]["TrustedModuleRequiredToBoot"] =
                    "Required";
            }
            else
            {
                aResp->res.jsonValue["Boot"]["TrustedModuleRequiredToBoot"] =
                    "Disabled";
            }
        });
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", int32_t(0),
        std::array<const char*, 1>{"xyz.openbmc_project.Control.TPM.Policy"});
}

/**
 * @brief Set TrustedModuleRequiredToBoot property. Determines whether or not
 * TPM is required for booting the host.
 *
 * @param[in] aResp         Shared pointer for generating response message.
 * @param[in] tpmRequired   Value to set TPM Required To Boot property to.
 *
 * @return None.
 */
inline void setTrustedModuleRequiredToBoot(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const bool tpmRequired)
{
    BMCWEB_LOG_DEBUG << "Set TrustedModuleRequiredToBoot.";

    crow::connections::systemBus->async_method_call(
        [aResp, tpmRequired](const boost::system::error_code ec,
                             dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "DBUS response error on TPM.Policy GetSubTree"
                             << ec;
            messages::internalError(aResp->res);
            return;
        }
        if (subtree.empty())
        {
            messages::propertyValueNotInList(aResp->res, "ComputerSystem",
                                             "TrustedModuleRequiredToBoot");
            return;
        }

        /* When there is more than one TPMEnable object... */
        if (subtree.size() > 1)
        {
            BMCWEB_LOG_DEBUG
                << "DBUS response has more than 1 TPM Enable object:"
                << subtree.size();
            // Throw an internal Error and return
            messages::internalError(aResp->res);
            return;
        }

        // Make sure the Dbus response map has a service and objectPath
        // field
        if (subtree[0].first.empty() || subtree[0].second.size() != 1)
        {
            BMCWEB_LOG_DEBUG << "TPM.Policy mapper error!";
            messages::internalError(aResp->res);
            return;
        }

        const std::string& path = subtree[0].first;
        const std::string& serv = subtree[0].second.begin()->first;

        if (serv.empty())
        {
            BMCWEB_LOG_DEBUG << "TPM.Policy service mapper error!";
            messages::internalError(aResp->res);
            return;
        }

        // Valid TPM Enable object found, now setting the value
        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec2) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR
                    << "DBUS response error: Set TrustedModuleRequiredToBoot"
                    << ec2;
                messages::internalError(aResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG << "Set TrustedModuleRequiredToBoot done.";
        },
            serv, path, "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.Control.TPM.Policy", "TPMEnable",
            dbus::utility::DbusVariantType(tpmRequired));
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", int32_t(0),
        std::array<const char*, 1>{"xyz.openbmc_project.Control.TPM.Policy"});
}

/**
 * @brief Sets AssetTag
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] assetTag  "AssetTag" from request.
 *
 * @return None.
 */
inline void setAssetTag(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                        const std::string& assetTag)
{
    crow::connections::systemBus->async_method_call(
        [aResp,
         assetTag](const boost::system::error_code ec,
                   const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree " << ec;
            messages::internalError(aResp->res);
            return;
        }
        if (subtree.empty())
        {
            BMCWEB_LOG_DEBUG << "Can't find system D-Bus object!";
            messages::internalError(aResp->res);
            return;
        }
        // Assume only 1 system D-Bus object
        // Throw an error if there is more than 1
        if (subtree.size() > 1)
        {
            BMCWEB_LOG_DEBUG << "Found more than 1 system D-Bus object!";
            messages::internalError(aResp->res);
            return;
        }
        if (subtree[0].first.empty() || subtree[0].second.size() != 1)
        {
            BMCWEB_LOG_DEBUG << "Asset Tag Set mapper error!";
            messages::internalError(aResp->res);
            return;
        }

        const std::string& path = subtree[0].first;
        const std::string& service = subtree[0].second.begin()->first;

        if (service.empty())
        {
            BMCWEB_LOG_DEBUG << "Asset Tag Set service mapper error!";
            messages::internalError(aResp->res);
            return;
        }

        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec2) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR << "D-Bus response error on AssetTag Set "
                                 << ec2;
                messages::internalError(aResp->res);
                return;
            }
        },
            service, path, "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.Inventory.Decorator.AssetTag", "AssetTag",
            dbus::utility::DbusVariantType(assetTag));
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.System"});
}

/**
 * @brief Validate the specified stopBootOnFault is valid and return the
 * stopBootOnFault name associated with that string
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] stopBootOnFaultString  String representing the desired
 * stopBootOnFault
 *
 * @return stopBootOnFault value or empty  if incoming value is not valid
 */
inline std::optional<bool>
    validstopBootOnFault(const std::string& stopBootOnFaultString)
{
    std::optional<bool> validstopBootEnabled;
    if (stopBootOnFaultString == "AnyFault")
    {
        return true;
    }
    if (stopBootOnFaultString == "Never")
    {
        return false;
    }
    return std::nullopt;
}

/**
 * @brief Sets stopBootOnFault
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] stopBootOnFault  "StopBootOnFault" from request.
 *
 * @return None.
 */
inline void setStopBootOnFault(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& stopBootOnFault)
{
    BMCWEB_LOG_DEBUG << "Set Stop Boot On Fault.";

    std::optional<bool> stopBootEnabled = validstopBootOnFault(stopBootOnFault);

    if (!stopBootEnabled)
    {
        BMCWEB_LOG_DEBUG << "Invalid property value for StopBootOnFault: "
                         << stopBootOnFault;
        messages::propertyValueNotInList(aResp->res, stopBootOnFault,
                                         "StopBootOnFault");
        return;
    }

    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/logging/settings",
        "xyz.openbmc_project.Logging.Settings", "QuiesceOnHwError",
        *stopBootEnabled, [aResp](const boost::system::error_code& ec) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR << "D-Bus response error: " << ec;
                messages::internalError(aResp->res);
            }
            return;
        }
    });
}

/**
 * @brief Sets automaticRetry (Auto Reboot)
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] automaticRetryConfig  "AutomaticRetryConfig" from request.
 *
 * @return None.
 */
inline void setAutomaticRetry(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                              const std::string& automaticRetryConfig)
{
    BMCWEB_LOG_DEBUG << "Set Automatic Retry.";

    // OpenBMC only supports "Disabled" and "RetryAttempts".
    bool autoRebootEnabled = false;

    if (automaticRetryConfig == "Disabled")
    {
        autoRebootEnabled = false;
    }
    else if (automaticRetryConfig == "RetryAttempts")
    {
        autoRebootEnabled = true;
    }
    else
    {
        BMCWEB_LOG_DEBUG << "Invalid property value for AutomaticRetryConfig: "
                         << automaticRetryConfig;
        messages::propertyValueNotInList(aResp->res, automaticRetryConfig,
                                         "AutomaticRetryConfig");
        return;
    }

    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "D-Bus response error: " << ec;
            messages::internalError(aResp->res);
            return;
        }
    },
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/control/host0/auto_reboot",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Boot.RebootPolicy", "AutoReboot",
        dbus::utility::DbusVariantType(autoRebootEnabled));
}

/**
 * @brief Sets power restore policy properties.
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] policy  power restore policy properties from request.
 *
 * @return None.
 */
inline void
    setPowerRestorePolicy(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const std::string& policy)
{
    BMCWEB_LOG_DEBUG << "Set power restore policy.";

    const boost::container::flat_map<std::string, std::string> policyMaps = {
        {"AlwaysOn",
         "xyz.openbmc_project.Control.Power.RestorePolicy.Policy.AlwaysOn"},
        {"AlwaysOff",
         "xyz.openbmc_project.Control.Power.RestorePolicy.Policy.AlwaysOff"},
        {"LastState",
         "xyz.openbmc_project.Control.Power.RestorePolicy.Policy.Restore"}};

    std::string powerRestorPolicy;

    auto policyMapsIt = policyMaps.find(policy);
    if (policyMapsIt == policyMaps.end())
    {
        messages::propertyValueNotInList(aResp->res, policy,
                                         "PowerRestorePolicy");
        return;
    }

    powerRestorPolicy = policyMapsIt->second;

    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "D-Bus response error: " << ec;
            messages::internalError(aResp->res);
            return;
        }
    },
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/control/host0/power_restore_policy",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Power.RestorePolicy", "PowerRestorePolicy",
        dbus::utility::DbusVariantType(powerRestorPolicy));
}

#ifdef BMCWEB_ENABLE_REDFISH_PROVISIONING_FEATURE
/**
 * @brief Retrieves provisioning status
 *
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void getProvisioningStatus(std::shared_ptr<bmcweb::AsyncResp> aResp)
{
    BMCWEB_LOG_DEBUG << "Get OEM information.";
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "xyz.openbmc_project.PFR.Manager",
        "/xyz/openbmc_project/pfr", "xyz.openbmc_project.PFR.Attributes",
        [aResp](const boost::system::error_code ec,
                const dbus::utility::DBusPropertiesMap& propertiesList) {
        nlohmann::json& oemPFR =
            aResp->res.jsonValue["Oem"]["OpenBmc"]["FirmwareProvisioning"];
        aResp->res.jsonValue["Oem"]["OpenBmc"]["@odata.type"] =
            "#OemComputerSystem.OpenBmc";
        oemPFR["@odata.type"] = "#OemComputerSystem.FirmwareProvisioning";

        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
            // not an error, don't have to have the interface
            oemPFR["ProvisioningStatus"] = "NotProvisioned";
            return;
        }

        const bool* provState = nullptr;
        const bool* lockState = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesList, "UfmProvisioned",
            provState, "UfmLocked", lockState);

        if (!success)
        {
            messages::internalError(aResp->res);
            return;
        }

        if ((provState == nullptr) || (lockState == nullptr))
        {
            BMCWEB_LOG_DEBUG << "Unable to get PFR attributes.";
            messages::internalError(aResp->res);
            return;
        }

        if (*provState == true)
        {
            if (*lockState == true)
            {
                oemPFR["ProvisioningStatus"] = "ProvisionedAndLocked";
            }
            else
            {
                oemPFR["ProvisioningStatus"] = "ProvisionedButNotLocked";
            }
        }
        else
        {
            oemPFR["ProvisioningStatus"] = "NotProvisioned";
        }
    });
}
#endif

/**
 * @brief Translate the PowerMode to a response message.
 *
 * @param[in] aResp  Shared pointer for generating response message.
 * @param[in] modeValue  PowerMode value to be translated
 *
 * @return None.
 */
inline void translatePowerMode(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& modeValue)
{
    if (modeValue == "xyz.openbmc_project.Control.Power.Mode.PowerMode.Static")
    {
        aResp->res.jsonValue["PowerMode"] = "Static";
    }
    else if (
        modeValue ==
        "xyz.openbmc_project.Control.Power.Mode.PowerMode.MaximumPerformance")
    {
        aResp->res.jsonValue["PowerMode"] = "MaximumPerformance";
    }
    else if (modeValue ==
             "xyz.openbmc_project.Control.Power.Mode.PowerMode.PowerSaving")
    {
        aResp->res.jsonValue["PowerMode"] = "PowerSaving";
    }
    else if (modeValue ==
             "xyz.openbmc_project.Control.Power.Mode.PowerMode.OEM")
    {
        aResp->res.jsonValue["PowerMode"] = "OEM";
    }
    else
    {
        // Any other values would be invalid
        BMCWEB_LOG_DEBUG << "PowerMode value was not valid: " << modeValue;
        messages::internalError(aResp->res);
    }
}

/**
 * @brief Retrieves system power mode
 *
 * @param[in] aResp  Shared pointer for generating response message.
 *
 * @return None.
 */
inline void getPowerMode(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get power mode.";

    // Get Power Mode object path:
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error on Power.Mode GetSubTree "
                             << ec;
            // This is an optional D-Bus object so just return if
            // error occurs
            return;
        }
        if (subtree.empty())
        {
            // As noted above, this is an optional interface so just return
            // if there is no instance found
            return;
        }
        if (subtree.size() > 1)
        {
            // More then one PowerMode object is not supported and is an
            // error
            BMCWEB_LOG_DEBUG
                << "Found more than 1 system D-Bus Power.Mode objects: "
                << subtree.size();
            messages::internalError(aResp->res);
            return;
        }
        if ((subtree[0].first.empty()) || (subtree[0].second.size() != 1))
        {
            BMCWEB_LOG_DEBUG << "Power.Mode mapper error!";
            messages::internalError(aResp->res);
            return;
        }
        const std::string& path = subtree[0].first;
        const std::string& service = subtree[0].second.begin()->first;
        if (service.empty())
        {
            BMCWEB_LOG_DEBUG << "Power.Mode service mapper error!";
            messages::internalError(aResp->res);
            return;
        }

        // Valid Power Mode object found, now read the current value
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, service, path,
            "xyz.openbmc_project.Control.Power.Mode",
            [aResp](const boost::system::error_code ec2,
                    const dbus::utility::DBusPropertiesMap& properties) {
            if (ec2)
            {
                // Service not available, no error, return no data
                BMCWEB_LOG_DEBUG << "Service not available on Power."
                                    "Mode properties GetAll: "
                                 << ec2;
                return;
            }

            const bool* safeMode = nullptr;
            const std::string* powerMode = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), properties, "SafeMode",
                safeMode, "PowerMode", powerMode);

            if (!success)
            {
                messages::internalError(aResp->res);
                return;
            }

            if (safeMode != nullptr)
            {
                BMCWEB_LOG_DEBUG << "Safe mode: " << *safeMode;
                nlohmann::json& oemSafeMode = aResp->res.jsonValue["Oem"];
                oemSafeMode["@odata.type"] = "#OemComputerSystem.Oem";
                oemSafeMode["IBM"]["@odata.type"] = "#OemComputerSystem.IBM";
                oemSafeMode["IBM"]["SafeMode"] = *safeMode;
            }
            if (powerMode != nullptr)
            {
                aResp->res.jsonValue["PowerMode@Redfish.AllowableValues"] = {
                    "Static", "MaximumPerformance", "PowerSaving"};

                BMCWEB_LOG_DEBUG << "Current power mode: " << *powerMode;

                translatePowerMode(aResp, *powerMode);
            }
        });
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", int32_t(0),
        std::array<const char*, 1>{"xyz.openbmc_project.Control.Power.Mode"});
}

/**
 * @brief Validate the specified mode is valid and return the PowerMode
 * name associated with that string
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] modeString  String representing the desired PowerMode
 *
 * @return PowerMode value or empty string if mode is not valid
 */
inline std::string
    validatePowerMode(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                      const std::string& modeString)
{
    std::string mode;

    if (modeString == "Static")
    {
        mode = "xyz.openbmc_project.Control.Power.Mode.PowerMode.Static";
    }
    else if (modeString == "MaximumPerformance")
    {
        mode =
            "xyz.openbmc_project.Control.Power.Mode.PowerMode.MaximumPerformance";
    }
    else if (modeString == "PowerSaving")
    {
        mode = "xyz.openbmc_project.Control.Power.Mode.PowerMode.PowerSaving";
    }
    else
    {
        messages::propertyValueNotInList(aResp->res, modeString, "PowerMode");
    }
    return mode;
}

/**
 * @brief Sets system power mode.
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] pmode   System power mode from request.
 *
 * @return None.
 */
inline void setPowerMode(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& pmode)
{
    BMCWEB_LOG_DEBUG << "Set power mode.";

    std::string powerMode = validatePowerMode(aResp, pmode);
    if (powerMode.empty())
    {
        return;
    }

    // Get Power Mode object path:
    crow::connections::systemBus->async_method_call(
        [aResp,
         powerMode](const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "DBUS response error on Power.Mode GetSubTree "
                             << ec;
            // This is an optional D-Bus object, but user attempted to patch
            messages::internalError(aResp->res);
            return;
        }
        if (subtree.empty())
        {
            // This is an optional D-Bus object, but user attempted to patch
            messages::resourceNotFound(aResp->res, "ComputerSystem",
                                       "PowerMode");
            return;
        }
        if (subtree.size() > 1)
        {
            // More then one PowerMode object is not supported and is an
            // error
            BMCWEB_LOG_DEBUG
                << "Found more than 1 system D-Bus Power.Mode objects: "
                << subtree.size();
            messages::internalError(aResp->res);
            return;
        }
        if ((subtree[0].first.empty()) || (subtree[0].second.size() != 1))
        {
            BMCWEB_LOG_DEBUG << "Power.Mode mapper error!";
            messages::internalError(aResp->res);
            return;
        }
        const std::string& path = subtree[0].first;
        const std::string& service = subtree[0].second.begin()->first;
        if (service.empty())
        {
            BMCWEB_LOG_DEBUG << "Power.Mode service mapper error!";
            messages::internalError(aResp->res);
            return;
        }

        BMCWEB_LOG_DEBUG << "Setting power mode(" << powerMode << ") -> "
                         << path;

        // Set the Power Mode property
        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec2) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR << "D-Bus response error: " << ec2;
                messages::internalError(aResp->res);
                return;
            }
        },
            service, path, "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.Control.Power.Mode", "PowerMode",
            dbus::utility::DbusVariantType(powerMode));
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", int32_t(0),
        std::array<const char*, 1>{"xyz.openbmc_project.Control.Power.Mode"});
}

/**
 * @brief Translates watchdog timeout action DBUS property value to redfish.
 *
 * @param[in] dbusAction    The watchdog timeout action in D-BUS.
 *
 * @return Returns as a string, the timeout action in Redfish terms. If
 * translation cannot be done, returns an empty string.
 */
inline std::string dbusToRfWatchdogAction(const std::string& dbusAction)
{
    if (dbusAction == "xyz.openbmc_project.State.Watchdog.Action.None")
    {
        return "None";
    }
    if (dbusAction == "xyz.openbmc_project.State.Watchdog.Action.HardReset")
    {
        return "ResetSystem";
    }
    if (dbusAction == "xyz.openbmc_project.State.Watchdog.Action.PowerOff")
    {
        return "PowerDown";
    }
    if (dbusAction == "xyz.openbmc_project.State.Watchdog.Action.PowerCycle")
    {
        return "PowerCycle";
    }

    return "";
}

/**
 *@brief Translates timeout action from Redfish to DBUS property value.
 *
 *@param[in] rfAction The timeout action in Redfish.
 *
 *@return Returns as a string, the time_out action as expected by DBUS.
 *If translation cannot be done, returns an empty string.
 */

inline std::string rfToDbusWDTTimeOutAct(const std::string& rfAction)
{
    if (rfAction == "None")
    {
        return "xyz.openbmc_project.State.Watchdog.Action.None";
    }
    if (rfAction == "PowerCycle")
    {
        return "xyz.openbmc_project.State.Watchdog.Action.PowerCycle";
    }
    if (rfAction == "PowerDown")
    {
        return "xyz.openbmc_project.State.Watchdog.Action.PowerOff";
    }
    if (rfAction == "ResetSystem")
    {
        return "xyz.openbmc_project.State.Watchdog.Action.HardReset";
    }

    return "";
}

/**
 * @brief Retrieves host watchdog timer properties over DBUS
 *
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void
    getHostWatchdogTimer(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get host watchodg";
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "xyz.openbmc_project.Watchdog",
        "/xyz/openbmc_project/watchdog/host0",
        "xyz.openbmc_project.State.Watchdog",
        [aResp](const boost::system::error_code ec,
                const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            // watchdog service is stopped
            BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
            return;
        }

        BMCWEB_LOG_DEBUG << "Got " << properties.size() << " wdt prop.";

        nlohmann::json& hostWatchdogTimer =
            aResp->res.jsonValue["HostWatchdogTimer"];

        // watchdog service is running/enabled
        hostWatchdogTimer["Status"]["State"] = "Enabled";

        const bool* enabled = nullptr;
        const std::string* expireAction = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "Enabled", enabled,
            "ExpireAction", expireAction);

        if (!success)
        {
            messages::internalError(aResp->res);
            return;
        }

        if (enabled != nullptr)
        {
            hostWatchdogTimer["FunctionEnabled"] = *enabled;
        }

        if (expireAction != nullptr)
        {
            std::string action = dbusToRfWatchdogAction(*expireAction);
            if (action.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            hostWatchdogTimer["TimeoutAction"] = action;
        }
    });
}

/**
 * @brief Sets Host WatchDog Timer properties.
 *
 * @param[in] aResp      Shared pointer for generating response message.
 * @param[in] wdtEnable  The WDTimer Enable value (true/false) from incoming
 *                       RF request.
 * @param[in] wdtTimeOutAction The WDT Timeout action, from incoming RF request.
 *
 * @return None.
 */
inline void setWDTProperties(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::optional<bool> wdtEnable,
                             const std::optional<std::string>& wdtTimeOutAction)
{
    BMCWEB_LOG_DEBUG << "Set host watchdog";

    if (wdtTimeOutAction)
    {
        std::string wdtTimeOutActStr = rfToDbusWDTTimeOutAct(*wdtTimeOutAction);
        // check if TimeOut Action is Valid
        if (wdtTimeOutActStr.empty())
        {
            BMCWEB_LOG_DEBUG << "Unsupported value for TimeoutAction: "
                             << *wdtTimeOutAction;
            messages::propertyValueNotInList(aResp->res, *wdtTimeOutAction,
                                             "TimeoutAction");
            return;
        }

        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DBUS response error " << ec;
                messages::internalError(aResp->res);
                return;
            }
        },
            "xyz.openbmc_project.Watchdog",
            "/xyz/openbmc_project/watchdog/host0",
            "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.State.Watchdog", "ExpireAction",
            dbus::utility::DbusVariantType(wdtTimeOutActStr));
    }

    if (wdtEnable)
    {
        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DBUS response error " << ec;
                messages::internalError(aResp->res);
                return;
            }
        },
            "xyz.openbmc_project.Watchdog",
            "/xyz/openbmc_project/watchdog/host0",
            "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.State.Watchdog", "Enabled",
            dbus::utility::DbusVariantType(*wdtEnable));
    }
}

/**
 * @brief Parse the Idle Power Saver properties into json
 *
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 * @param[in] properties  IPS property data from DBus.
 *
 * @return true if successful
 */
inline bool
    parseIpsProperties(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                       const dbus::utility::DBusPropertiesMap& properties)
{
    const bool* enabled = nullptr;
    const uint8_t* enterUtilizationPercent = nullptr;
    const uint64_t* enterDwellTime = nullptr;
    const uint8_t* exitUtilizationPercent = nullptr;
    const uint64_t* exitDwellTime = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "Enabled", enabled,
        "EnterUtilizationPercent", enterUtilizationPercent, "EnterDwellTime",
        enterDwellTime, "ExitUtilizationPercent", exitUtilizationPercent,
        "ExitDwellTime", exitDwellTime);

    if (!success)
    {
        return false;
    }

    if (enabled != nullptr)
    {
        aResp->res.jsonValue["IdlePowerSaver"]["Enabled"] = *enabled;
    }

    if (enterUtilizationPercent != nullptr)
    {
        aResp->res.jsonValue["IdlePowerSaver"]["EnterUtilizationPercent"] =
            *enterUtilizationPercent;
    }

    if (enterDwellTime != nullptr)
    {
        const std::chrono::duration<uint64_t, std::milli> ms(*enterDwellTime);
        aResp->res.jsonValue["IdlePowerSaver"]["EnterDwellTimeSeconds"] =
            std::chrono::duration_cast<std::chrono::duration<uint64_t>>(ms)
                .count();
    }

    if (exitUtilizationPercent != nullptr)
    {
        aResp->res.jsonValue["IdlePowerSaver"]["ExitUtilizationPercent"] =
            *exitUtilizationPercent;
    }

    if (exitDwellTime != nullptr)
    {
        const std::chrono::duration<uint64_t, std::milli> ms(*exitDwellTime);
        aResp->res.jsonValue["IdlePowerSaver"]["ExitDwellTimeSeconds"] =
            std::chrono::duration_cast<std::chrono::duration<uint64_t>>(ms)
                .count();
    }

    return true;
}

/**
 * @brief Retrieves host watchdog timer properties over DBUS
 *
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void getIdlePowerSaver(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get idle power saver parameters";

    // Get IdlePowerSaver object path:
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR
                << "DBUS response error on Power.IdlePowerSaver GetSubTree "
                << ec;
            messages::internalError(aResp->res);
            return;
        }
        if (subtree.empty())
        {
            // This is an optional interface so just return
            // if there is no instance found
            BMCWEB_LOG_DEBUG << "No instances found";
            return;
        }
        if (subtree.size() > 1)
        {
            // More then one PowerIdlePowerSaver object is not supported and
            // is an error
            BMCWEB_LOG_DEBUG << "Found more than 1 system D-Bus "
                                "Power.IdlePowerSaver objects: "
                             << subtree.size();
            messages::internalError(aResp->res);
            return;
        }
        if ((subtree[0].first.empty()) || (subtree[0].second.size() != 1))
        {
            BMCWEB_LOG_DEBUG << "Power.IdlePowerSaver mapper error!";
            messages::internalError(aResp->res);
            return;
        }
        const std::string& path = subtree[0].first;
        const std::string& service = subtree[0].second.begin()->first;
        if (service.empty())
        {
            BMCWEB_LOG_DEBUG << "Power.IdlePowerSaver service mapper error!";
            messages::internalError(aResp->res);
            return;
        }

        // Valid IdlePowerSaver object found, now read the current values
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, service, path,
            "xyz.openbmc_project.Control.Power.IdlePowerSaver",
            [aResp](const boost::system::error_code ec2,
                    const dbus::utility::DBusPropertiesMap& properties) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR
                    << "DBUS response error on IdlePowerSaver GetAll: " << ec2;
                messages::internalError(aResp->res);
                return;
            }

            if (!parseIpsProperties(aResp, properties))
            {
                messages::internalError(aResp->res);
                return;
            }
        });
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Control.Power.IdlePowerSaver"});

    BMCWEB_LOG_DEBUG << "EXIT: Get idle power saver parameters";
}

/*
 * Handle Enabled Panel Functions
 */
inline void doGetEnabledPanelFunctions(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::function<void(const std::vector<uint8_t>&)>&& callback)
{
    BMCWEB_LOG_DEBUG << "Get Enabled Panel functions";

    crow::connections::systemBus->async_method_call(
        [asyncResp, callback](const boost::system::error_code& ec,
                              const std::vector<uint8_t>& enabledFuncs) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "Get Enabled Panel Functions D-bus error: "
                             << ec.value();
            messages::internalError(asyncResp->res);
            return;
        }
        callback(enabledFuncs);
    },
        "com.ibm.PanelApp", "/com/ibm/panel_app", "com.ibm.panel",
        "getEnabledFunctions");
}

/*
 * Get Enabled Panel Functions
 */
inline void getEnabledPanelFunctions(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    doGetEnabledPanelFunctions(
        asyncResp, [asyncResp](const std::vector<uint8_t>& enabledFuncs) {
        nlohmann::json& oem = asyncResp->res.jsonValue["Oem"];
        oem["@odata.type"] = "#OemComputerSystem.Oem";
        oem["IBM"]["@odata.type"] = "#OemComputerSystem.IBM";
        oem["IBM"]["EnabledPanelFunctions"] = enabledFuncs;
    });
}

/**
 * Execute a Panel Enabled Function
 */
inline void
    executePanelFunction(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const uint8_t funcNo)
{
    BMCWEB_LOG_DEBUG << "Execute Panel function " << std::to_string(funcNo);

    crow::connections::systemBus->async_method_call(
        [asyncResp,
         funcNo](const boost::system::error_code& ec,
                 const sdbusplus::message_t& msg,
                 const std::tuple<bool, std::string, std::string>& result) {
        if (ec)
        {
            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError == nullptr)
            {
                BMCWEB_LOG_ERROR << "Execute a panel function D-bus error:  "
                                 << ec.value();
                messages::internalError(asyncResp->res);
                return;
            }
            if (dbusError->name ==
                std::string_view("xyz.openbmc_project.Common.Error.NotAllowed"))
            {
                BMCWEB_LOG_ERROR << "PanelFunction " << std::to_string(funcNo)
                                 << " is not enabled";
                messages::operationNotAllowed(asyncResp->res);
                return;
            }
            else if (dbusError->name ==
                     std::string_view(
                         "xyz.openbmc_project.Common.Error.InternalFailure"))
            {
                BMCWEB_LOG_ERROR << "ExecutePanelFunction "
                                 << std::to_string(funcNo) << " is failed";
                messages::operationFailed(asyncResp->res);
                return;
            }
            BMCWEB_LOG_ERROR << "Execute a panel function D-bus error:  "
                             << ec.value();
            messages::internalError(asyncResp->res);
            return;
        }

        if (!std::get<0>(result))
        {
            BMCWEB_LOG_ERROR << "ExecutePanelFunction "
                             << std::to_string(funcNo) << " is failed";
            messages::operationFailed(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["Result"] = {std::get<1>(result),
                                              std::get<2>(result)};
        messages::success(asyncResp->res);
    },
        "com.ibm.PanelApp", "/com/ibm/panel_app", "com.ibm.panel",
        "ExecuteFunction", funcNo);
}

inline void handleSystemActionsOemExecutePanelFunctionPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG << "handleSystemActionsOemExecutePanelFunctionPost...";
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    uint8_t funcNo;
    if (!json_util::readJsonAction(req, asyncResp->res, "FuncNo", funcNo))
    {
        BMCWEB_LOG_DEBUG << "Missing funcNo";
        messages::actionParameterMissing(asyncResp->res, "ExecutePanelFunction",
                                         "FuncNo");
        return;
    }

    doGetEnabledPanelFunctions(
        asyncResp,
        [funcNo, asyncResp](const std::vector<uint8_t>& enabledFuncs) {
        auto it = std::find(enabledFuncs.begin(), enabledFuncs.end(), funcNo);
        if (it == enabledFuncs.end())
        {
            BMCWEB_LOG_ERROR << "PanelFunction " << std::to_string(funcNo)
                             << " is not enabled";
            messages::operationNotAllowed(asyncResp->res);
            return;
        }
        executePanelFunction(asyncResp, funcNo);
    });
}

/**
 * SystemActionsOemExecutePanelFunction class supports handle POST method for
 * ExecutePanelFunction  action. The class retrieves and sends data directly to
 * D-Bus.
 */
inline void requestRoutesSystemActionsOemExecutePanelFunction(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/system/Actions/Oem/OemComputerSystem.ExecutePanelFunction/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleSystemActionsOemExecutePanelFunctionPost, std::ref(app)));
}

/**
 * @brief Sets Idle Power Saver properties.
 *
 * @param[in] aResp      Shared pointer for generating response message.
 * @param[in] ipsEnable  The IPS Enable value (true/false) from incoming
 *                       RF request.
 * @param[in] ipsEnterUtil The utilization limit to enter idle state.
 * @param[in] ipsEnterTime The time the utilization must be below ipsEnterUtil
 * before entering idle state.
 * @param[in] ipsExitUtil The utilization limit when exiting idle state.
 * @param[in] ipsExitTime The time the utilization must be above ipsExutUtil
 * before exiting idle state
 *
 * @return None.
 */
inline void setIdlePowerSaver(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                              const std::optional<bool> ipsEnable,
                              const std::optional<uint8_t> ipsEnterUtil,
                              const std::optional<uint64_t> ipsEnterTime,
                              const std::optional<uint8_t> ipsExitUtil,
                              const std::optional<uint64_t> ipsExitTime)
{
    BMCWEB_LOG_DEBUG << "Set idle power saver properties";

    // Get IdlePowerSaver object path:
    crow::connections::systemBus->async_method_call(
        [aResp, ipsEnable, ipsEnterUtil, ipsEnterTime, ipsExitUtil,
         ipsExitTime](const boost::system::error_code ec,
                      const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR
                << "DBUS response error on Power.IdlePowerSaver GetSubTree "
                << ec;
            messages::internalError(aResp->res);
            return;
        }
        if (subtree.empty())
        {
            // This is an optional D-Bus object, but user attempted to patch
            messages::resourceNotFound(aResp->res, "ComputerSystem",
                                       "IdlePowerSaver");
            return;
        }
        if (subtree.size() > 1)
        {
            // More then one PowerIdlePowerSaver object is not supported and
            // is an error
            BMCWEB_LOG_DEBUG
                << "Found more than 1 system D-Bus Power.IdlePowerSaver objects: "
                << subtree.size();
            messages::internalError(aResp->res);
            return;
        }
        if ((subtree[0].first.empty()) || (subtree[0].second.size() != 1))
        {
            BMCWEB_LOG_DEBUG << "Power.IdlePowerSaver mapper error!";
            messages::internalError(aResp->res);
            return;
        }
        const std::string& path = subtree[0].first;
        const std::string& service = subtree[0].second.begin()->first;
        if (service.empty())
        {
            BMCWEB_LOG_DEBUG << "Power.IdlePowerSaver service mapper error!";
            messages::internalError(aResp->res);
            return;
        }

        // Valid Power IdlePowerSaver object found, now set any values that
        // need to be updated

        if (ipsEnable)
        {
            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code ec2) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR << "DBUS response error " << ec2;
                    messages::internalError(aResp->res);
                    return;
                }
            },
                service, path, "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.Control.Power.IdlePowerSaver", "Enabled",
                dbus::utility::DbusVariantType(*ipsEnable));
        }
        if (ipsEnterUtil)
        {
            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code ec2) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR << "DBUS response error " << ec2;
                    messages::internalError(aResp->res);
                    return;
                }
            },
                service, path, "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.Control.Power.IdlePowerSaver",
                "EnterUtilizationPercent",
                dbus::utility::DbusVariantType(*ipsEnterUtil));
        }
        if (ipsEnterTime)
        {
            // Convert from seconds into milliseconds for DBus
            const uint64_t timeMilliseconds = *ipsEnterTime * 1000;
            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code ec2) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR << "DBUS response error " << ec2;
                    messages::internalError(aResp->res);
                    return;
                }
            },
                service, path, "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.Control.Power.IdlePowerSaver",
                "EnterDwellTime",
                dbus::utility::DbusVariantType(timeMilliseconds));
        }
        if (ipsExitUtil)
        {
            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code ec2) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR << "DBUS response error " << ec2;
                    messages::internalError(aResp->res);
                    return;
                }
            },
                service, path, "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.Control.Power.IdlePowerSaver",
                "ExitUtilizationPercent",
                dbus::utility::DbusVariantType(*ipsExitUtil));
        }
        if (ipsExitTime)
        {
            // Convert from seconds into milliseconds for DBus
            const uint64_t timeMilliseconds = *ipsExitTime * 1000;
            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code ec2) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR << "DBUS response error " << ec2;
                    messages::internalError(aResp->res);
                    return;
                }
            },
                service, path, "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.Control.Power.IdlePowerSaver",
                "ExitDwellTime",
                dbus::utility::DbusVariantType(timeMilliseconds));
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Control.Power.IdlePowerSaver"});

    BMCWEB_LOG_DEBUG << "EXIT: Set idle power saver parameters";
}

inline void handleComputerSystemHead(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ComputerSystemCollection/ComputerSystemCollection.json>; rel=describedby");
}

/**
 * SystemsCollection derived class for delivering ComputerSystems Collection
 * Schema
 */
inline void requestRoutesSystemsCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/")
        .privileges(redfish::privileges::headComputerSystemCollection)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleComputerSystemHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/")
        .privileges(redfish::privileges::getComputerSystemCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/ComputerSystemCollection.json>; rel=describedby");
        asyncResp->res.jsonValue["@odata.type"] =
            "#ComputerSystemCollection.ComputerSystemCollection";
        asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Systems";
        asyncResp->res.jsonValue["Name"] = "Computer System Collection";

        sdbusplus::asio::getProperty<std::string>(
            *crow::connections::systemBus,
            "xyz.openbmc_project.Network.Hypervisor",
            "/xyz/openbmc_project/network/hypervisor/config",
            "xyz.openbmc_project.Network.SystemConfiguration", "HostName",
            [asyncResp](const boost::system::error_code ec2,
                        const std::string& /*hostName*/) {
            nlohmann::json& ifaceArray = asyncResp->res.jsonValue["Members"];
            ifaceArray = nlohmann::json::array();
            auto& resCount = asyncResp->res.jsonValue["Members@odata.count"];

            nlohmann::json::object_t system;
            system["@odata.id"] = "/redfish/v1/Systems/system";
            ifaceArray.push_back(std::move(system));
            resCount = ifaceArray.size();
            if (!ec2)
            {
                BMCWEB_LOG_DEBUG << "Hypervisor is available";
                nlohmann::json::object_t hypervisor;
                hypervisor["@odata.id"] = "/redfish/v1/Systems/hypervisor";
                ifaceArray.push_back(std::move(hypervisor));
                resCount = ifaceArray.size();
            }
        });
    });
}

/**
 * Function transceives data with dbus directly.
 */
inline void doNMI(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr const char* serviceName = "xyz.openbmc_project.Control.Host.NMI";
    constexpr const char* objectPath = "/xyz/openbmc_project/control/host0/nmi";
    constexpr const char* interfaceName =
        "xyz.openbmc_project.Control.Host.NMI";
    constexpr const char* method = "NMI";

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << " Bad D-Bus request error: " << ec;
            messages::internalError(asyncResp->res);
            return;
        }
        messages::success(asyncResp->res);
    },
        serviceName, objectPath, interfaceName, method);
}

/**
 * Handle error responses from d-bus for system power requests
 */
inline void handleSystemActionResetError(const boost::system::error_code& ec,
                                         const sdbusplus::message_t& eMsg,
                                         std::string_view resetType,
                                         crow::Response& res)
{
    if (ec.value() == boost::asio::error::invalid_argument)
    {
        messages::actionParameterNotSupported(res, resetType, "Reset");
        return;
    }

    if (eMsg.get_error() == nullptr)
    {
        BMCWEB_LOG_ERROR << "D-Bus response error: " << ec;
        messages::internalError(res);
        return;
    }
    std::string_view errorMessage = eMsg.get_error()->name;

    // If operation failed due to BMC not being in Ready state, tell
    // user to retry in a bit
    if ((errorMessage ==
         std::string_view(
             "xyz.openbmc_project.State.Chassis.Error.BMCNotReady")) ||
        (errorMessage ==
         std::string_view("xyz.openbmc_project.State.Host.Error.BMCNotReady")))
    {
        BMCWEB_LOG_DEBUG << "BMC not ready, operation not allowed right now";
        messages::serviceTemporarilyUnavailable(res, "10");
        return;
    }

    BMCWEB_LOG_ERROR << "System Action Reset transition fail " << ec
                     << " sdbusplus:" << errorMessage;
    messages::internalError(res);
}

/**
 * SystemActionsReset class supports handle POST method for Reset action.
 * The class retrieves and sends data directly to D-Bus.
 */
inline void requestRoutesSystemActionsReset(App& app)
{
    /**
     * Function handles POST method request.
     * Analyzes POST body message before sends Reset request data to D-Bus.
     */
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/system/Actions/ComputerSystem.Reset/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::string resetType;
        if (!json_util::readJsonAction(req, asyncResp->res, "ResetType",
                                       resetType))
        {
            return;
        }

        // Get the command and host vs. chassis
        std::string command;
        bool hostCommand = true;
        if ((resetType == "On") || (resetType == "ForceOn"))
        {
            command = "xyz.openbmc_project.State.Host.Transition.On";
            hostCommand = true;
        }
        else if (resetType == "ForceOff")
        {
            command = "xyz.openbmc_project.State.Chassis.Transition.Off";
            hostCommand = false;
        }
        else if (resetType == "GracefulShutdown")
        {
            command = "xyz.openbmc_project.State.Host.Transition.Off";
            hostCommand = true;
        }
        else if (resetType == "GracefulRestart")
        {
            command =
                "xyz.openbmc_project.State.Host.Transition.GracefulWarmReboot";
            hostCommand = true;
        }
        else if (resetType == "PowerCycle")
        {
            command = "xyz.openbmc_project.State.Host.Transition.Reboot";
            hostCommand = true;
        }
        else if (resetType == "Nmi")
        {
            doNMI(asyncResp);
            return;
        }
        else
        {
            messages::actionParameterUnknown(asyncResp->res, "Reset",
                                             resetType);
            return;
        }

        if (hostCommand)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp, resetType](const boost::system::error_code& ec,
                                       sdbusplus::message_t& sdbusErrMsg) {
                if (ec)
                {
                    handleSystemActionResetError(ec, sdbusErrMsg, resetType,
                                                 asyncResp->res);

                    return;
                }
                messages::success(asyncResp->res);
            },
                "xyz.openbmc_project.State.Host",
                "/xyz/openbmc_project/state/host0",
                "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.State.Host", "RequestedHostTransition",
                dbus::utility::DbusVariantType{command});
        }
        else
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp, resetType](const boost::system::error_code& ec,
                                       sdbusplus::message_t& sdbusErrMsg) {
                if (ec)
                {
                    handleSystemActionResetError(ec, sdbusErrMsg, resetType,
                                                 asyncResp->res);
                    return;
                }
                messages::success(asyncResp->res);
            },
                "xyz.openbmc_project.State.Chassis",
                "/xyz/openbmc_project/state/chassis0",
                "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.State.Chassis", "RequestedPowerTransition",
                dbus::utility::DbusVariantType{command});
        }
    });
}

inline void handleComputerSystemCollectionHead(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ComputerSystem/ComputerSystem.json>; rel=describedby");
}

inline void afterPortRequest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const std::vector<std::tuple<std::string, std::string, bool>>& socketData)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR << "D-Bus response error: " << ec;
        messages::internalError(asyncResp->res);
        return;
    }
    for (const auto& data : socketData)
    {
        const std::string& socketPath = get<0>(data);
        const std::string& protocolName = get<1>(data);
        bool isProtocolEnabled = get<2>(data);
        nlohmann::json& dataJson = asyncResp->res.jsonValue["SerialConsole"];
        dataJson[protocolName]["ServiceEnabled"] = isProtocolEnabled;
        // need to retrieve port number for
        // obmc-console-ssh service
        if (protocolName == "SSH")
        {
            getPortNumber(socketPath, [asyncResp, protocolName](
                                          const boost::system::error_code ec1,
                                          int portNumber) {
                if (ec1)
                {
                    BMCWEB_LOG_ERROR << "D-Bus response error: " << ec1;
                    messages::internalError(asyncResp->res);
                    return;
                }
                nlohmann::json& dataJson1 =
                    asyncResp->res.jsonValue["SerialConsole"];
                dataJson1[protocolName]["Port"] = portNumber;
            });
        }
    }
}
/**
 * Systems derived class for delivering Computer Systems Schema.
 */
inline void requestRoutesSystems(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/")
        .privileges(redfish::privileges::headComputerSystem)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleComputerSystemCollectionHead, std::ref(app)));
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        std::string systemName = "system";
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/ComputerSystem/ComputerSystem.json>; rel=describedby");
        asyncResp->res.jsonValue["@odata.type"] =
            "#ComputerSystem.v1_16_0.ComputerSystem";
        asyncResp->res.jsonValue["Name"] = "system";
        asyncResp->res.jsonValue["Id"] = "system";
        asyncResp->res.jsonValue["SystemType"] = "Physical";
        asyncResp->res.jsonValue["Description"] = "Computer System";
        asyncResp->res.jsonValue["ProcessorSummary"]["Count"] = 0;
        asyncResp->res.jsonValue["ProcessorSummary"]["Status"]["State"] =
            "Disabled";
        asyncResp->res.jsonValue["MemorySummary"]["TotalSystemMemoryGiB"] =
            double(0);
        asyncResp->res.jsonValue["MemorySummary"]["Status"]["State"] =
            "Disabled";
        asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Systems/system";

        asyncResp->res.jsonValue["Processors"]["@odata.id"] =
            "/redfish/v1/Systems/system/Processors";
        asyncResp->res.jsonValue["Memory"]["@odata.id"] =
            "/redfish/v1/Systems/system/Memory";
        asyncResp->res.jsonValue["Storage"]["@odata.id"] =
            "/redfish/v1/Systems/system/Storage";
        asyncResp->res.jsonValue["FabricAdapters"]["@odata.id"] =
            "/redfish/v1/Systems/system/FabricAdapters";

        asyncResp->res.jsonValue["Actions"]["#ComputerSystem.Reset"]["target"] =
            "/redfish/v1/Systems/system/Actions/ComputerSystem.Reset";
        asyncResp->res.jsonValue["Actions"]["#ComputerSystem.Reset"]
                                ["@Redfish.ActionInfo"] =
            "/redfish/v1/Systems/system/ResetActionInfo";

        asyncResp->res.jsonValue["LogServices"]["@odata.id"] =
            "/redfish/v1/Systems/system/LogServices";
        asyncResp->res.jsonValue["Bios"]["@odata.id"] =
            "/redfish/v1/Systems/system/Bios";

        nlohmann::json::array_t managedBy;
        nlohmann::json& manager = managedBy.emplace_back();
        manager["@odata.id"] = "/redfish/v1/Managers/bmc";
        asyncResp->res.jsonValue["Links"]["ManagedBy"] = std::move(managedBy);
        asyncResp->res.jsonValue["Status"]["Health"] = "OK";
        asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

        // Fill in SerialConsole info
        asyncResp->res.jsonValue["SerialConsole"]["MaxConcurrentSessions"] = 15;
        asyncResp->res.jsonValue["SerialConsole"]["IPMI"]["ServiceEnabled"] =
            true;

        // TODO (Gunnar): Should look for obmc-console-ssh@2200.service
        asyncResp->res.jsonValue["SerialConsole"]["SSH"]["ServiceEnabled"] =
            true;
        asyncResp->res.jsonValue["SerialConsole"]["SSH"]["Port"] = 2200;
        asyncResp->res
            .jsonValue["SerialConsole"]["SSH"]["HotKeySequenceDisplay"] =
            "Press ~. to exit console";
        getPortStatusAndPath(std::span{protocolToDBusForSystems},
                             std::bind_front(afterPortRequest, asyncResp));

#ifdef BMCWEB_ENABLE_KVM
        // Fill in GraphicalConsole info
        asyncResp->res.jsonValue["GraphicalConsole"]["ServiceEnabled"] = true;
        asyncResp->res.jsonValue["GraphicalConsole"]["MaxConcurrentSessions"] =
            4;
        asyncResp->res.jsonValue["GraphicalConsole"]["ConnectTypesSupported"] =
            nlohmann::json::array_t({"KVMIP"});
#endif // BMCWEB_ENABLE_KVM

        // Execute Panel Function
        nlohmann::json& executePanelFunction =
            asyncResp->res
                .jsonValue["Actions"]["Oem"]
                          ["#OemComputerSystem.v1_0_0.ExecutePanelFunction"];
        executePanelFunction["target"] =
            "/redfish/v1/Systems/system/Actions/Oem/OemComputerSystem.ExecutePanelFunction";

        getMainChassisId(asyncResp,
                         [](const std::string& chassisId,
                            const std::shared_ptr<bmcweb::AsyncResp>& aRsp) {
            nlohmann::json::array_t chassisArray;
            nlohmann::json& chassis = chassisArray.emplace_back();
            chassis["@odata.id"] = "/redfish/v1/Chassis/" + chassisId;
            aRsp->res.jsonValue["Links"]["Chassis"] = std::move(chassisArray);
        });

        redfish::systems_utils::getValidSystemsPath(
            asyncResp, systemName,
            [asyncResp,
             systemName](const std::optional<std::string>& validSystemsPath) {
            if (!validSystemsPath)
            {
                messages::resourceNotFound(asyncResp->res, "Systems",
                                           systemName);
                return;
            }
            getLocationIndicatorActive(asyncResp, *validSystemsPath);
        });

        // TODO (Gunnar): Remove IndicatorLED after enough time has passed
        getIndicatorLedState(asyncResp);
        getComputerSystem(asyncResp);
        getHostState(asyncResp);
        getBootProgress(asyncResp);
        getBootProgressLastStateTime(asyncResp);
        getPCIeDeviceList(asyncResp, "PCIeDevices");
        getHostWatchdogTimer(asyncResp);
        getPowerRestorePolicy(asyncResp);
        getStopBootOnFault(asyncResp);
        getAutomaticRetry(asyncResp);
        getLastResetTime(asyncResp);
#ifdef BMCWEB_ENABLE_IBM_LED_EXTENSIONS
        getLampTestState(asyncResp);
        getSAI(asyncResp, "PartitionSystemAttentionIndicator");
        getSAI(asyncResp, "PlatformSystemAttentionIndicator");
#endif
#ifdef BMCWEB_ENABLE_REDFISH_PROVISIONING_FEATURE
        getProvisioningStatus(asyncResp);
#endif
        getTrustedModuleRequiredToBoot(asyncResp);
        getPowerMode(asyncResp);
        getIdlePowerSaver(asyncResp);
        getEnabledPanelFunctions(asyncResp);
    });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/")
        .privileges(redfish::privileges::patchComputerSystem)
        .methods(boost::beast::http::verb::patch)(
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

        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/ComputerSystem/ComputerSystem.json>; rel=describedby");

        std::optional<bool> locationIndicatorActive;
        std::optional<std::string> indicatorLed;
        std::optional<std::string> assetTag;
        std::optional<std::string> powerRestorePolicy;
        std::optional<std::string> powerMode;
        std::optional<bool> wdtEnable;
        std::optional<std::string> wdtTimeOutAction;
        std::optional<std::string> bootAutomaticRetry;
        std::optional<bool> bootTrustedModuleRequired;
        std::optional<std::string> stopBootOnFault;
        std::optional<bool> ipsEnable;
        std::optional<uint8_t> ipsEnterUtil;
        std::optional<uint64_t> ipsEnterTime;
        std::optional<uint8_t> ipsExitUtil;
        std::optional<uint64_t> ipsExitTime;
        std::optional<nlohmann::json> oem;

        // clang-format off
                if (!json_util::readJsonPatch(
                        req, asyncResp->res,
                        "IndicatorLED", indicatorLed,
                        "LocationIndicatorActive", locationIndicatorActive,
                        "AssetTag", assetTag,
                        "PowerRestorePolicy", powerRestorePolicy,
                        "PowerMode", powerMode,
                        "HostWatchdogTimer/FunctionEnabled", wdtEnable,
                        "HostWatchdogTimer/TimeoutAction", wdtTimeOutAction,
                        "Boot/AutomaticRetryConfig", bootAutomaticRetry,
                        "Boot/TrustedModuleRequiredToBoot", bootTrustedModuleRequired,
                        "Boot/StopBootOnFault", stopBootOnFault,
                        "IdlePowerSaver/Enabled", ipsEnable,
                        "IdlePowerSaver/EnterUtilizationPercent", ipsEnterUtil,
                        "IdlePowerSaver/EnterDwellTimeSeconds", ipsEnterTime,
                        "IdlePowerSaver/ExitUtilizationPercent", ipsExitUtil,
                        "IdlePowerSaver/ExitDwellTimeSeconds", ipsExitTime,
                        "Oem", oem))
                {
                    return;
                }
        // clang-format on

        asyncResp->res.result(boost::beast::http::status::no_content);

        if (assetTag)
        {
            setAssetTag(asyncResp, *assetTag);
        }

        if (wdtEnable || wdtTimeOutAction)
        {
            setWDTProperties(asyncResp, wdtEnable, wdtTimeOutAction);
        }

        if (bootAutomaticRetry)
        {
            setAutomaticRetry(asyncResp, *bootAutomaticRetry);
        }

        if (bootTrustedModuleRequired)
        {
            setTrustedModuleRequiredToBoot(asyncResp,
                                           *bootTrustedModuleRequired);
        }

        if (stopBootOnFault)
        {
            setStopBootOnFault(asyncResp, *stopBootOnFault);
        }

        if (locationIndicatorActive)
        {
            redfish::systems_utils::getValidSystemsPath(
                asyncResp, systemName,
                [asyncResp, systemName,
                 locationIndicatorActive{*locationIndicatorActive}](
                    const std::optional<std::string>& validSystemsPath) {
                if (!validSystemsPath)
                {
                    messages::resourceNotFound(asyncResp->res, "Systems",
                                               systemName);
                    return;
                }
                setLocationIndicatorActive(asyncResp, *validSystemsPath,
                                           locationIndicatorActive);
            });
        }

        // TODO (Gunnar): Remove IndicatorLED after enough time has
        // passed
        if (indicatorLed)
        {
            setIndicatorLedState(asyncResp, *indicatorLed);
            asyncResp->res.addHeader(boost::beast::http::field::warning,
                                     "299 - \"IndicatorLED is deprecated. Use "
                                     "LocationIndicatorActive instead.\"");
        }

        if (powerRestorePolicy)
        {
            setPowerRestorePolicy(asyncResp, *powerRestorePolicy);
        }

        if (powerMode)
        {
            setPowerMode(asyncResp, *powerMode);
        }

        if (oem)
        {
            std::optional<nlohmann::json> ibmOem;
            if (!redfish::json_util::readJson(*oem, asyncResp->res, "IBM",
                                              ibmOem))
            {
                return;
            }

            if (ibmOem)
            {
#ifdef BMCWEB_ENABLE_IBM_LED_EXTENSIONS
                std::optional<bool> lampTest;
                std::optional<bool> partitionSAI;
                std::optional<bool> platformSAI;
                std::optional<bool> pcieTopologyRefresh;
                std::optional<bool> savePCIeTopologyInfo;
                if (!json_util::readJson(
                        *ibmOem, asyncResp->res, "LampTest", lampTest,
                        "PartitionSystemAttentionIndicator", partitionSAI,
                        "PlatformSystemAttentionIndicator", platformSAI,
                        "PCIeTopologyRefresh", pcieTopologyRefresh,
                        "SavePCIeTopologyInfo", savePCIeTopologyInfo))
                {
                    return;
                }
                if (lampTest)
                {
                    setLampTestState(asyncResp, *lampTest);
                }
                if (partitionSAI)
                {
                    setSAI(asyncResp, "PartitionSystemAttentionIndicator",
                           *partitionSAI);
                }
                if (platformSAI)
                {
                    setSAI(asyncResp, "PlatformSystemAttentionIndicator",
                           *platformSAI);
                }
#else
                std::optional<bool> pcieTopologyRefresh;
                std::optional<bool> savePCIeTopologyInfo;
                if (!json_util::readJson(
                        *ibmOem, asyncResp->res, "PCIeTopologyRefresh",
                        pcieTopologyRefresh, "SavePCIeTopologyInfo",
                        savePCIeTopologyInfo))
                {
                    return;
                }
#endif
                if (pcieTopologyRefresh)
                {
                    setPCIeTopologyRefresh(req, asyncResp,
                                           *pcieTopologyRefresh);
                }
                if (savePCIeTopologyInfo)
                {
                    setSavePCIeTopologyInfo(asyncResp, *savePCIeTopologyInfo);
                }
            }
        }

        if (ipsEnable || ipsEnterUtil || ipsEnterTime || ipsExitUtil ||
            ipsExitTime)
        {
            setIdlePowerSaver(asyncResp, ipsEnable, ipsEnterUtil, ipsEnterTime,
                              ipsExitUtil, ipsExitTime);
        }
    });
}

inline void handleSystemCollectionResetActionHead(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ActionInfo/ActionInfo.json>; rel=describedby");
}

/**
 * SystemResetActionInfo derived class for delivering Computer Systems
 * ResetType AllowableValues using ResetInfo schema.
 */
inline void requestRoutesSystemResetActionInfo(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/ResetActionInfo/")
        .privileges(redfish::privileges::headActionInfo)
        .methods(boost::beast::http::verb::head)(std::bind_front(
            handleSystemCollectionResetActionHead, std::ref(app)));
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/ResetActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/ActionInfo/ActionInfo.json>; rel=describedby");

        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/system/ResetActionInfo";
        asyncResp->res.jsonValue["@odata.type"] =
            "#ActionInfo.v1_1_2.ActionInfo";
        asyncResp->res.jsonValue["Name"] = "Reset Action Info";
        asyncResp->res.jsonValue["Id"] = "ResetActionInfo";

        nlohmann::json::array_t parameters;
        nlohmann::json::object_t parameter;

        parameter["Name"] = "ResetType";
        parameter["Required"] = true;
        parameter["DataType"] = "String";
        nlohmann::json::array_t allowableValues;
        allowableValues.emplace_back("On");
        allowableValues.emplace_back("ForceOff");
        allowableValues.emplace_back("ForceOn");
        allowableValues.emplace_back("GracefulRestart");
        allowableValues.emplace_back("GracefulShutdown");
        allowableValues.emplace_back("PowerCycle");
        allowableValues.emplace_back("Nmi");
        parameter["AllowableValues"] = std::move(allowableValues);
        parameters.emplace_back(std::move(parameter));

        asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
    });
}
} // namespace redfish
