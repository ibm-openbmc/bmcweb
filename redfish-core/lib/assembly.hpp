#pragma once

#include "led.hpp"

#include <sdbusplus/unpack_properties.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/name_utils.hpp>

#include <cstddef>
#include <string>

namespace redfish
{
using VariantType = std::variant<bool, std::string, uint64_t, uint32_t>;

constexpr std::array<const char*, 9> chassisAssemblyIfaces = {
    "xyz.openbmc_project.Inventory.Item.Vrm",
    "xyz.openbmc_project.Inventory.Item.Tpm",
    "xyz.openbmc_project.Inventory.Item.Panel",
    "xyz.openbmc_project.Inventory.Item.Battery",
    "xyz.openbmc_project.Inventory.Item.DiskBackplane",
    "xyz.openbmc_project.Inventory.Item.Board",
    "xyz.openbmc_project.Inventory.Item.Connector",
    "xyz.openbmc_project.Inventory.Item.Drive",
    "xyz.openbmc_project.Inventory.Item.Board.Motherboard"};

static void assembleAssemblyProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const dbus::utility::DBusPropertiesMap& properties,
    nlohmann::json& assemblyData, const std::string& path)
{
    const std::string* partNumber = nullptr;
    const std::string* serialNumber = nullptr;
    const std::string* sparePartNumber = nullptr;
    const std::string* model = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "PartNumber", partNumber,
        "SerialNumber", serialNumber, "SparePartNumber", sparePartNumber,
        "Model", model);

    if (!success)
    {
        messages::internalError(aResp->res);
        BMCWEB_LOG_ERROR << "Could not read one or more properties from "
                         << path;
        return;
    }

    if (partNumber != nullptr)
    {
        assemblyData["PartNumber"] = *partNumber;
    }
    if (serialNumber != nullptr)
    {
        assemblyData["SerialNumber"] = *serialNumber;
    }
    if (sparePartNumber != nullptr)
    {
        assemblyData["SparePartNumber"] = *sparePartNumber;
    }
    if (model != nullptr)
    {
        assemblyData["Model"] = *model;
    }
}

/**
 * @brief Get properties for the assemblies associated to given chassis
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisPath - Chassis the assemblies are associated with.
 * @param[in] assemblies - list of all the assemblies associated with the
 * chassis.
 * @return None.
 */
inline void
    getAssemblyProperties(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const std::string& chassisPath,
                          const std::vector<std::string>& assemblies)
{
    BMCWEB_LOG_DEBUG << "Get properties for assembly associated";

    const std::string& chassis =
        sdbusplus::message::object_path(chassisPath).filename();

    aResp->res.jsonValue["Assemblies@odata.count"] = assemblies.size();

    std::size_t assemblyIndex = 0;
    for (const auto& assembly : assemblies)
    {
        nlohmann::json& tempyArray = aResp->res.jsonValue["Assemblies"];

        std::string dataID = "/redfish/v1/Chassis/" + chassis +
                             "/Assembly#/Assemblies/";
        dataID.append(std::to_string(assemblyIndex));

        tempyArray.push_back({{"@odata.type", "#Assembly.v1_3_0.AssemblyData"},
                              {"@odata.id", dataID},
                              {"MemberId", std::to_string(assemblyIndex)}});

        tempyArray.at(assemblyIndex)["Name"] =
            sdbusplus::message::object_path(assembly).filename();

        // Handle special case for tod_battery assembly OEM ReadyToRemove
        // property NOTE: The following method for the special case of the
        // tod_battery ReadyToRemove property only works when there is only ONE
        // adcsensor handled by the adcsensor application.
        if (sdbusplus::message::object_path(assembly).filename() ==
            "tod_battery")
        {
            tempyArray.at(assemblyIndex)["Oem"]["OpenBMC"]["@odata.type"] =
                "#OemAssembly.v1_0_0.Assembly";

            crow::connections::systemBus->async_method_call(
                [aResp, assemblyIndex](const boost::system::error_code ec) {
                if (ec)
                {
                    if (ec.value() == 5)
                    {
                        // Battery voltage is not on DBUS so ADCSensor is not
                        // running.
                        nlohmann::json& assemblyArray =
                            aResp->res.jsonValue["Assemblies"];
                        assemblyArray.at(
                            assemblyIndex)["Oem"]["OpenBMC"]["ReadyToRemove"] =
                            true;
                        return;
                    }
                    BMCWEB_LOG_DEBUG << "DBUS response error" << ec.value();
                    messages::internalError(aResp->res);
                    return;
                }

                nlohmann::json& assemblyArray =
                    aResp->res.jsonValue["Assemblies"];

                assemblyArray.at(
                    assemblyIndex)["Oem"]["OpenBMC"]["ReadyToRemove"] = false;
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject",
                "/xyz/openbmc_project/sensors/voltage/Battery_Voltage",
                std::array<const char*, 0>{});
        }

        crow::connections::systemBus->async_method_call(
            [aResp, assemblyIndex, assembly](
                const boost::system::error_code ec,
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>& object) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);
                return;
            }

            nlohmann::json::json_pointer ptr(
                "/Assemblies/" + std::to_string(assemblyIndex) + "/Name");

            name_util::getPrettyName(aResp, assembly, object[0].first, ptr);

            for (const auto& [serviceName, interfaceList] : object)
            {
                for (const auto& interface : interfaceList)
                {
                    if (interface ==
                        "xyz.openbmc_project.Inventory.Decorator.Asset")
                    {
                        sdbusplus::asio::getAllProperties(
                            *crow::connections::systemBus, serviceName,
                            assembly, interface,
                            [aResp, assemblyIndex,
                             assembly](const boost::system::error_code ec2,
                                       const dbus::utility::DBusPropertiesMap&
                                           properties) {
                            if (ec2)
                            {
                                BMCWEB_LOG_DEBUG << "DBUS response error";
                                messages::internalError(aResp->res);
                                return;
                            }

                            nlohmann::json& assemblyArray =
                                aResp->res.jsonValue["Assemblies"];
                            nlohmann::json& assemblyData =
                                assemblyArray.at(assemblyIndex);

                            assembleAssemblyProperties(aResp, properties,
                                                       assemblyData, assembly);
                        });
                    }
                    else if (interface == "xyz.openbmc_project.Inventory."
                                          "Decorator.LocationCode")
                    {
                        sdbusplus::asio::getProperty<std::string>(
                            *crow::connections::systemBus, serviceName,
                            assembly, interface, "LocationCode",
                            [aResp,
                             assemblyIndex](const boost::system::error_code ec3,
                                            const std::string& property) {
                            if (ec3)
                            {
                                BMCWEB_LOG_DEBUG << "DBUS response error";
                                messages::internalError(aResp->res);
                                return;
                            }

                            nlohmann::json& assemblyArray =
                                aResp->res.jsonValue["Assemblies"];
                            nlohmann::json& assemblyData =
                                assemblyArray.at(assemblyIndex);

                            assemblyData["Location"]["PartLocation"]
                                        ["ServiceLabel"] = property;
                        });
                    }
                    else if (interface == "xyz.openbmc_project.State."
                                          "Decorator.OperationalStatus")
                    {
                        sdbusplus::asio::getProperty<bool>(
                            *crow::connections::systemBus, serviceName,
                            assembly,
                            "xyz.openbmc_project.State.Decorator.OperationalStatus",
                            "Functional",
                            [aResp, assemblyIndex](
                                const boost::system::error_code& ec4,
                                bool functional) {
                            if (ec4)
                            {
                                BMCWEB_LOG_ERROR << "DBUS response error "
                                                 << ec4;
                                messages::internalError(aResp->res);
                                return;
                            }

                            nlohmann::json& assemblyArray =
                                aResp->res.jsonValue["Assemblies"];
                            nlohmann::json& assemblyData =
                                assemblyArray.at(assemblyIndex);

                            if (!functional)
                            {
                                assemblyData["Status"]["Health"] = "Critical";
                            }
                            else
                            {
                                assemblyData["Status"]["Health"] = "OK";
                            }
                        });
                    }
                    else if (interface == "xyz.openbmc_project.Inventory.Item")
                    {
                        sdbusplus::asio::getProperty<bool>(
                            *crow::connections::systemBus, serviceName,
                            assembly, "xyz.openbmc_project.Inventory.Item",
                            "Present",
                            [aResp, assemblyIndex,
                             assembly](const boost::system::error_code ec2,
                                       const std::variant<bool>& property) {
                            if (ec2)
                            {
                                BMCWEB_LOG_DEBUG << "DBUS response error";
                                messages::internalError(aResp->res);
                                return;
                            }

                            std::string fru =
                                sdbusplus::message::object_path(assembly)
                                    .filename();

                            nlohmann::json& assemblyArray =
                                aResp->res.jsonValue["Assemblies"];
                            nlohmann::json& assemblyData =
                                assemblyArray.at(assemblyIndex);

                            const bool* value = std::get_if<bool>(&property);

                            if (value == nullptr)
                            {
                                // illegal value
                                messages::internalError(aResp->res);
                                return;
                            }

                            // Special handling for LCD and base
                            // panel CM.
                            if (fru == "panel0" || fru == "panel1")
                            {
                                assemblyData["Oem"]["OpenBMC"]["@odata.type"] =
                                    "#OemAssembly.v1_0_"
                                    "0.Assembly";

                                // if panel is not present,
                                // implies it is already
                                // removed or can be placed.
                                assemblyData["Oem"]["OpenBMC"]
                                            ["ReadyToRemove"] = !(*value);
                            }

                            if (!*value)
                            {
                                assemblyData["Status"]["State"] = "Absent";
                            }
                            else
                            {
                                assemblyData["Status"]["State"] = "Enabled";
                            }
                        });
                    }
                }
            }

            nlohmann::json& assemblyArray = aResp->res.jsonValue["Assemblies"];
            nlohmann::json& assemblyData = assemblyArray.at(assemblyIndex);
            getLocationIndicatorActive(aResp, assembly, assemblyData);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject", assembly,
            chassisAssemblyIfaces);

        assemblyIndex++;
    }
}

static void
    startOrStopADCSensor(const bool start,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string method{"StartUnit"};
    if (!start)
    {
        method = "StopUnit";
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "Failed to start or stop ADCSensor:" << ec;
            messages::internalError(asyncResp->res);
            return;
        }
        messages::success(asyncResp->res);
    },
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", method,
        "xyz.openbmc_project.adcsensor.service", "replace");
}

static void doBatteryCM(const std::string& assembly, const bool readyToRemove,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (readyToRemove)
    {
        // Stop the adcsensor service so it doesn't monitor the battery
        startOrStopADCSensor(false, asyncResp);
        return;
    }

    // Find the service that has the OperationalStatus iface, set the
    // Functional property back to true, and then start the adcsensor service.
    crow::connections::systemBus->async_method_call(
        [asyncResp, assembly](const boost::system::error_code ec,
                              const dbus::utility::MapperGetObject& object) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error";
            messages::internalError(asyncResp->res);
            return;
        }

        for (const auto& [serviceName, interfaceList] : object)
        {
            auto ifaceIt = std::find(
                interfaceList.begin(), interfaceList.end(),
                "xyz.openbmc_project.State.Decorator.OperationalStatus");

            if (ifaceIt == interfaceList.end())
            {
                continue;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, assembly](const boost::system::error_code ec2) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR << "Failed to set functional property "
                                        "on battery "
                                     << ec2;
                    messages::internalError(asyncResp->res);
                    return;
                }
                startOrStopADCSensor(true, asyncResp);
            },
                serviceName, assembly, "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.State.Decorator."
                "OperationalStatus",
                "Functional", std::variant<bool>(true));
            return;
        }

        BMCWEB_LOG_ERROR << "No OperationalStatus interface on " << assembly;
        messages::internalError(asyncResp->res);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", assembly,
        std::array<const char*, 1>{
            "xyz.openbmc_project.State.Decorator.OperationalStatus"});
}

/**
 * @brief Set location indicator for the assemblies associated to given
 * chassis
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassis - Chassis the assemblies are associated with.
 * @param[in] assemblies - list of all the assemblies associated with the
 * chassis.
 * @param[in] req - The request data
 * @return None.
 */
inline void setAssemblylocationIndicators(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassis, const std::vector<std::string>& assemblies,
    const crow::Request& req)
{
    BMCWEB_LOG_DEBUG
        << "Set locationIndicator for assembly associated to chassis ="
        << chassis;

    std::optional<std::vector<nlohmann::json>> assemblyData;
    if (!json_util::readJsonAction(req, asyncResp->res, "Assemblies",
                                   assemblyData))
    {
        return;
    }
    if (!assemblyData)
    {
        return;
    }

    std::vector<nlohmann::json> items = std::move(*assemblyData);
    std::map<std::string, bool> locationIndicatorActiveMap;
    std::map<std::string, nlohmann::json> oemIndicatorMap;

    for (auto& item : items)
    {
        std::optional<std::string> memberId;
        std::optional<bool> locationIndicatorActive;
        std::optional<nlohmann::json> oem;

        if (!json_util::readJson(
                item, asyncResp->res, "LocationIndicatorActive",
                locationIndicatorActive, "MemberId", memberId, "Oem", oem))
        {
            return;
        }

        if (locationIndicatorActive)
        {
            if (memberId)
            {
                locationIndicatorActiveMap[*memberId] =
                    *locationIndicatorActive;
            }
            else
            {
                BMCWEB_LOG_ERROR << "Property Missing ";
                BMCWEB_LOG_ERROR
                    << "MemberId must be included with LocationIndicatorActive ";
                messages::propertyMissing(asyncResp->res, "MemberId");
                return;
            }
        }

        if (oem)
        {
            if (memberId)
            {
                oemIndicatorMap[*memberId] = *oem;
            }
            else
            {
                BMCWEB_LOG_ERROR << "Property Missing ";
                BMCWEB_LOG_ERROR
                    << "MemberId must be included with the Oem property ";
                messages::propertyMissing(asyncResp->res, "MemberId");
                return;
            }
        }
    }

    std::size_t assemblyIndex = 0;
    for (const auto& assembly : assemblies)
    {
        auto iter =
            locationIndicatorActiveMap.find(std::to_string(assemblyIndex));

        if (iter != locationIndicatorActiveMap.end())
        {
            setLocationIndicatorActive(asyncResp, assembly, iter->second);
        }

        auto iter2 = oemIndicatorMap.find(std::to_string(assemblyIndex));

        if (iter2 != oemIndicatorMap.end())
        {
            std::optional<nlohmann::json> openbmc;
            if (!json_util::readJson(iter2->second, asyncResp->res, "OpenBMC",
                                     openbmc))
            {
                BMCWEB_LOG_ERROR << "Property Value Format Error ";
                messages::propertyValueFormatError(
                    asyncResp->res,
                    (*openbmc).dump(2, ' ', true,
                                    nlohmann::json::error_handler_t::replace),
                    "OpenBMC");
                return;
            }

            if (!openbmc)
            {
                BMCWEB_LOG_ERROR << "Property Missing ";
                messages::propertyMissing(asyncResp->res, "OpenBMC");
                return;
            }

            std::optional<bool> readytoremove;
            if (!json_util::readJson(*openbmc, asyncResp->res, "ReadyToRemove",
                                     readytoremove))
            {
                BMCWEB_LOG_ERROR << "Property Value Format Error ";
                messages::propertyValueFormatError(
                    asyncResp->res,
                    (*openbmc).dump(2, ' ', true,
                                    nlohmann::json::error_handler_t::replace),
                    "ReadyToRemove");
                return;
            }

            if (!readytoremove)
            {
                BMCWEB_LOG_ERROR << "Property Missing ";
                messages::propertyMissing(asyncResp->res, "ReadyToRemove");
                return;
            }

            // Handle special case for tod_battery assembly OEM ReadyToRemove
            // property. NOTE: The following method for the special case of the
            // tod_battery ReadyToRemove property only works when there is only
            // ONE adcsensor handled by the adcsensor application.
            if (sdbusplus::message::object_path(assembly).filename() ==
                "tod_battery")
            {
                doBatteryCM(assembly, readytoremove.value(), asyncResp);
            }

            // Special handling for LCD and base panel. This is required to
            // support concurrent maintenance for base and LCD panel.
            else if (sdbusplus::message::object_path(assembly).filename() ==
                         "panel0" ||
                     sdbusplus::message::object_path(assembly).filename() ==
                         "panel1")
            {
                // Based on the status of readytoremove flag, inventory data
                // like CCIN and present property needs to be updated for this
                // FRU.
                // readytoremove as true implies FRU has been prepared for
                // removal. Set action as "deleteFRUVPD". This is the api
                // exposed by vpd-manager to clear CCIN and set present
                // property as false for the FRU.
                // readytoremove as false implies FRU has been replaced. Set
                // action as "CollectFRUVPD". This is the api exposed by
                // vpd-manager to recollect vpd for a given FRU.
                std::string action = (readytoremove.value()) ? "deleteFRUVPD"
                                                             : "CollectFRUVPD";

                crow::connections::systemBus->async_method_call(
                    [asyncResp, action](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR
                            << "Call to Manager failed for action: " << action
                            << " with error " << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }
                },
                    "com.ibm.VPD.Manager", "/com/ibm/VPD/Manager",
                    "com.ibm.VPD.Manager", action,
                    sdbusplus::message::object_path(assembly));
            }
            else
            {
                BMCWEB_LOG_ERROR
                    << "Property Unknown: ReadyToRemove on Assembly with MemberID: "
                    << assemblyIndex;
                messages::propertyUnknown(asyncResp->res, "ReadyToRemove");
                return;
            }
        }
        assemblyIndex++;
    }
}

/**
 * @brief Api to check if the assemblies fetched from association Json is also
 * implemented in the system. In case the interface for that assembly is not
 * found update the list and fetch properties for only implemented assemblies.
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisPath - Chassis the assemblies are associated with.
 * @param[in] assemblies - list of all the assemblies associated with the
 * chassis.
 * @param[in] setLocationIndicatorActiveFlag - The doPatch flag.
 * @param[in] req - The request data.
 * @return None.
 */
inline void checkAssemblyInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisPath, dbus::utility::MapperEndPoints& assemblies,
    const bool& setLocationIndicatorActiveFlag, const crow::Request& req)
{
    crow::connections::systemBus->async_method_call(
        [aResp, chassisPath, assemblies, setLocationIndicatorActiveFlag, req](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "D-Bus response error on GetSubTree " << ec;
            messages::internalError(aResp->res);
            return;
        }

        if (subtree.empty())
        {
            BMCWEB_LOG_DEBUG << "No object paths found";
            return;
        }
        std::vector<std::string> updatedAssemblyList;
        for (const auto& [objectPath, serviceName] : subtree)
        {
            // This list will store common paths between assemblies fetched
            // from association json and assemblies which are actually
            // implemeted. This is to handle the case in which there is
            // entry in association json but implementation of interface for
            // that particular assembly is missing.
            auto it = std::find(assemblies.begin(), assemblies.end(),
                                objectPath);
            if (it != assemblies.end())
            {
                updatedAssemblyList.emplace(updatedAssemblyList.end(), *it);
            }
        }

        if (!updatedAssemblyList.empty())
        {
            // sorting is required to facilitate patch as the array does not
            // store and data which can be mapped back to Dbus path of
            // assembly.
            std::sort(updatedAssemblyList.begin(), updatedAssemblyList.end());

            if (setLocationIndicatorActiveFlag)
            {
                setAssemblylocationIndicators(aResp, chassisPath,
                                              updatedAssemblyList, req);
            }
            else
            {
                getAssemblyProperties(aResp, chassisPath, updatedAssemblyList);
            }
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", int32_t(0), chassisAssemblyIfaces);
}

/**
 * @brief Api to get assembly endpoints from mapper.
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisPath - Chassis to which the assemblies are
 * associated.
 * @param[in] setLocationIndicatorActiveFlag - The doPatch flag.
 * @param[in] req - The request data.
 * @return None.
 */
inline void
    getAssemblyEndpoints(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& chassisPath,
                         const bool& setLocationIndicatorActiveFlag,
                         const crow::Request& req)
{
    BMCWEB_LOG_DEBUG << "Get assembly endpoints";

    sdbusplus::message::object_path assemblyPath(chassisPath);
    assemblyPath /= "assembly";

    // if there is assembly association, look
    // for endpoints
    dbus::utility::getAssociationEndPoints(
        assemblyPath, [aResp, chassisPath, setLocationIndicatorActiveFlag,
                       req](const boost::system::error_code ec,
                            const dbus::utility::MapperEndPoints& endpoints) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response "
                                "error";
            messages::internalError(aResp->res);
            return;
        }

        dbus::utility::MapperEndPoints sortedAssemblyList = endpoints;
        std::sort(sortedAssemblyList.begin(), sortedAssemblyList.end());

        checkAssemblyInterface(aResp, chassisPath, sortedAssemblyList,
                               setLocationIndicatorActiveFlag, req);
        return;
    });
}

/**
 * @brief Api to check for assembly associations.
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisPath - Chassis to which the assemblies are
 * associated.
 * @param[in] setLocationIndicatorActiveFlag - The doPatch flag.
 * @param[in] req - The request data.
 * @return None.
 */
inline void checkForAssemblyAssociations(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisPath, const std::string& service,
    const bool& setLocationIndicatorActiveFlag, const crow::Request& req)
{
    BMCWEB_LOG_DEBUG << "Check for assembly association";

    dbus::utility::getAssociationList(
        service, chassisPath,
        [aResp, chassisPath, setLocationIndicatorActiveFlag,
         req](const boost::system::error_code ec,
              const dbus::utility::AssociationList& associations) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error";
            messages::internalError(aResp->res);
            return;
        }

        bool isAssmeblyAssociation = false;
        for (const auto& listOfAssociations : associations)
        {
            if (std::get<0>(listOfAssociations) != "assembly")
            {
                // implies this is not an assembly
                // association
                continue;
            }

            isAssmeblyAssociation = true;
            break;
        }

        if (isAssmeblyAssociation)
        {
            getAssemblyEndpoints(aResp, chassisPath,
                                 setLocationIndicatorActiveFlag, req);
        }
    });
}

/**
 * @brief Api to check if there is any association.
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisPath - Chassis to which the assemblies are
 * associated.
 * @param[in] setLocationIndicatorActiveFlag - The doPatch flag.
 * @param[in] req - The request data.
 * @return None.
 */
inline void checkAssociation(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& chassisPath,
                             const bool& setLocationIndicatorActiveFlag,
                             const crow::Request& req)
{
    BMCWEB_LOG_DEBUG << "Check chassis for association";

    std::string chassis =
        sdbusplus::message::object_path(chassisPath).filename();
    if (chassis.empty())
    {
        BMCWEB_LOG_ERROR << "Failed to find / in Chassis path";
        messages::internalError(aResp->res);
        return;
    }

    if (!setLocationIndicatorActiveFlag)
    {
        aResp->res.jsonValue["Assemblies"] = nlohmann::json::array();
        aResp->res.jsonValue["Assemblies@odata.count"] = 0;
    }

    // check if this chassis hosts any association
    crow::connections::systemBus->async_method_call(
        [aResp, chassisPath, setLocationIndicatorActiveFlag, req](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error";
            messages::internalError(aResp->res);
            return;
        }

        for (const auto& [serviceName, interfaceList] : object)
        {
            for (const auto& interface : interfaceList)
            {
                if (interface == "xyz.openbmc_project.Association.Definitions")
                {
                    checkForAssemblyAssociations(
                        aResp, chassisPath, serviceName,
                        setLocationIndicatorActiveFlag, req);

                    return;
                }
            }
        }
        return;
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", chassisPath,
        std::array<const char*, 0>{});
}

namespace assembly
{
/**
 * @brief Get chassis path with given chassis ID
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisID - Chassis to which the assemblies are
 * associated.
 * @param[in] setLocationIndicatorActiveFlag - The doPatch flag.
 * @param[in] req - The request data.
 * @return None.
 */
inline void getChassis(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                       const std::string& chassisID,
                       const bool& setLocationIndicatorActiveFlag,
                       const crow::Request& req)
{
    BMCWEB_LOG_DEBUG << "Get chassis path";

    // get the chassis path
    crow::connections::systemBus->async_method_call(
        [aResp, chassisID, setLocationIndicatorActiveFlag,
         req](const boost::system::error_code ec,
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

            if (!setLocationIndicatorActiveFlag)
            {
                aResp->res.jsonValue["@odata.type"] =
                    "#Assembly.v1_3_0.Assembly";
                aResp->res.jsonValue["@odata.id"] = "/redfish/v1/Chassis/" +
                                                    chassisID + "/Assembly";
                aResp->res.jsonValue["Name"] = "Assembly Collection";
                aResp->res.jsonValue["Id"] = "Assembly";
            }

            checkAssociation(aResp, path, setLocationIndicatorActiveFlag, req);
            return;
        }

        BMCWEB_LOG_ERROR << "Chassis not found";
        messages::resourceNotFound(aResp->res, "Chassis", chassisID);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.Chassis"});
}

/**
 * @brief API used to fill the Assembly id of the assembled object that
 *        assembled in the given assembly parent object path.
 *
 *        bmcweb using the sequential numeric value by sorting the
 *        assembled objects instead of the assembled object dbus id
 *        for the Redfish Assembly implementation.
 *
 * @param[in] aResp - The redfish response to return.
 * @param[in] assemblyParentServ - The assembly parent dbus service name.
 * @param[in] assemblyParentObjPath - The assembly parent dbus object path.
 * @param[in] assemblyParentIface - The assembly parent dbus interface name
 *                                  to valid the supports in the bmcweb.
 * @param[in] assemblyUriPropPath - The redfish property path to fill with id.
 * @param[in] assembledObjPath - The assembled object that need to fill with
 *                               its id. Used to check in the parent assembly
 *                               associations.
 * @param[in] assembledUriVal - The assembled object redfish uri value that
 *                              need to replace with its id.
 *
 * @return The redfish response with assembled object id in the given
 *         redfish property path if success else returns the error.
 */
inline void fillWithAssemblyId(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& assemblyParentServ,
    const sdbusplus::message::object_path& assemblyParentObjPath,
    const std::string& assemblyParentIface,
    const nlohmann::json::json_pointer& assemblyUriPropPath,
    const sdbusplus::message::object_path& assembledObjPath,
    const std::string& assembledUriVal)
{
    if (assemblyParentIface != "xyz.openbmc_project.Inventory.Item.Chassis")
    {
        // Currently, bmcweb supporting only chassis assembly uri so return
        // error if unsupported assembly uri interface was given
        BMCWEB_LOG_ERROR << "Unsupported interface [" << assemblyParentIface
                         << "] was given to fill assembly id. "
                         << "Please add support in the bmcweb";
        messages::internalError(aResp->res);
        return;
    }

    using associationList =
        std::vector<std::tuple<std::string, std::string, std::string>>;

    sdbusplus::asio::getProperty<associationList>(
        *crow::connections::systemBus, assemblyParentServ,
        assemblyParentObjPath.str,
        "xyz.openbmc_project.Association.Definitions", "Associations",
        [aResp, assemblyUriPropPath, assemblyParentObjPath, assembledObjPath,
         assembledUriVal](const boost::system::error_code ec,
                          const associationList& associations) {
        if (ec)
        {
            BMCWEB_LOG_ERROR
                << "DBUS response error [" << ec.value() << " : "
                << ec.message() << "] when tried to get the Associations from ["
                << assemblyParentObjPath.str
                << "] to fill Assembly id of the assembled object ["
                << assembledObjPath.str << "]";
            messages::internalError(aResp->res);
            return;
        }

        std::vector<std::string> assemblyAssoc;
        for (const auto& association : associations)
        {
            if (std::get<0>(association) != "assembly")
            {
                continue;
            }
            assemblyAssoc.emplace_back(std::get<2>(association));
        }

        if (assemblyAssoc.empty())
        {
            BMCWEB_LOG_ERROR
                << "No assembly associations in the ["
                << assemblyParentObjPath.str
                << "] to fill Assembly id of the assembled object ["
                << assembledObjPath.str << "]";
            messages::internalError(aResp->res);
            return;
        }

        // Mak sure whether the retrieved assembly associations are
        // implemented before finding the assembly id as per bmcweb
        // Assembly design.
        crow::connections::systemBus->async_method_call(
            [aResp, assemblyUriPropPath, assemblyParentObjPath,
             assembledObjPath, assemblyAssoc, assembledUriVal](
                const boost::system::error_code ec1,
                const std::vector<std::pair<
                    std::string, std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>>&
                    objects) {
            if (ec1)
            {
                BMCWEB_LOG_ERROR << "DBUS response error [" << ec1.value()
                                 << " : " << ec1.message()
                                 << "] when tried to get the subtree to check "
                                 << "assembled objects implementation of the ["
                                 << assemblyParentObjPath.str
                                 << "] to find assembled object id of the ["
                                 << assembledObjPath.str
                                 << "] to fill in the URI property";
                messages::internalError(aResp->res);
                return;
            }

            if (objects.empty())
            {
                BMCWEB_LOG_ERROR
                    << "No objects in the [" << assemblyParentObjPath.str
                    << "] to check assembled objects implementation "
                    << "to fill the assembled object [ " << assembledObjPath.str
                    << "] id in the URI property";
                messages::internalError(aResp->res);
                return;
            }

            std::vector<std::string> implAssemblyAssocs;
            for (const auto& object : objects)
            {
                auto it = std::find(assemblyAssoc.begin(), assemblyAssoc.end(),
                                    object.first);
                if (it != assemblyAssoc.end())
                {
                    implAssemblyAssocs.emplace_back(*it);
                }
            }

            if (implAssemblyAssocs.empty())
            {
                BMCWEB_LOG_ERROR
                    << "The assembled objects of the ["
                    << assemblyParentObjPath.str << "] are not implemented so "
                    << "unable to fill the assembled object [ "
                    << assembledObjPath.str << "] id in the URI property";
                messages::internalError(aResp->res);
                return;
            }

            // sort the implemented assemply object as per bmcweb design
            // to match with Assembly GET and PATCH handler.
            std::sort(implAssemblyAssocs.begin(), implAssemblyAssocs.end());

            auto assembledObjectIt = std::find(implAssemblyAssocs.begin(),
                                               implAssemblyAssocs.end(),
                                               assembledObjPath.str);

            if (assembledObjectIt == implAssemblyAssocs.end())
            {
                BMCWEB_LOG_ERROR << "The assembled object ["
                                 << assembledObjPath.str << "] in the object ["
                                 << assemblyParentObjPath.str
                                 << "] is not implemented so unable to fill "
                                 << "assembled object id in the URI property";
                messages::internalError(aResp->res);
                return;
            }

            auto assembledObjectId = std::distance(implAssemblyAssocs.begin(),
                                                   assembledObjectIt);

            std::string::size_type assembledObjectNamePos =
                assembledUriVal.rfind(assembledObjPath.filename());

            if (assembledObjectNamePos == std::string::npos)
            {
                BMCWEB_LOG_ERROR << "The assembled object name ["
                                 << assembledObjPath.filename() << "] is not "
                                 << "found in the redfish property value ["
                                 << assembledUriVal
                                 << "] to replace with assembled object id ["
                                 << assembledObjectId << "]";
                messages::internalError(aResp->res);
                return;
            }
            std::string uriValwithId(assembledUriVal);
            uriValwithId.replace(assembledObjectNamePos,
                                 assembledObjPath.filename().length(),
                                 std::to_string(assembledObjectId));

            aResp->res.jsonValue[assemblyUriPropPath] = uriValwithId;
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", int32_t(0),
            chassisAssemblyIfaces);
    });
}

} // namespace assembly

/**
 * Systems derived class for delivering Assembly Schema.
 */
inline void requestRoutesAssembly(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Assembly/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisId) {
        const bool setLocationIndicatorActiveFlag = false;

        BMCWEB_LOG_DEBUG << "chassis =" << chassisId;
        assembly::getChassis(asyncResp, chassisId,
                             setLocationIndicatorActiveFlag, req);
    });

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Assembly/")
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::patch)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisID) {
        const bool setLocationIndicatorActiveFlag = true;

        BMCWEB_LOG_DEBUG << "Chassis = " << chassisID;
        assembly::getChassis(asyncResp, chassisID,
                             setLocationIndicatorActiveFlag, req);
    });
}
} // namespace redfish
