#pragma once

#include "led.hpp"

#include <app.hpp>
#include <boost/system/error_code.hpp>
#include <dbus_utility.hpp>
#include <pcie.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/json_utils.hpp>
#include <utils/pcie_util.hpp>

namespace redfish
{

inline std::string analysisGeneration(const std::string& pcieType)
{
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen1")
    {
        return "Gen1";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen2")
    {
        return "Gen2";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen3")
    {
        return "Gen3";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen4")
    {
        return "Gen4";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen5")
    {
        return "Gen5";
    }

    // Unknown or others
    return "";
}

inline std::string analysisSlotType(const std::string& slotType)
{
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.FullLength")
    {
        return "FullLength";
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.HalfLength")
    {
        return "HalfLength";
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.LowProfile")
    {
        return "LowProfile";
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.Mini")
    {
        return "Mini";
    }
    if (slotType == "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.M_2")
    {
        return "M2";
    }
    if (slotType == "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.OEM")
    {
        return "OEM";
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.OCP3Small")
    {
        return "OCP3Small";
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.OCP3Large")
    {
        return "OCP3Large";
    }
    if (slotType == "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.U_2")
    {
        return "U2";
    }

    // Unknown or others
    return "";
}

inline void
    addLinkedPcieDevices(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& slotPath, size_t index)
{
    // Collect device associated with this slot and
    // populate it here
    crow::connections::systemBus->async_method_call(
        [asyncResp, index](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            if (subtree.size() == 0)
            {
                BMCWEB_LOG_DEBUG
                    << "Can't find PCIeDevice D-Bus object for given slot";
                return;
            }

            // Assuming only one device path per slot.
            const std::string& pcieDevciePath = std::get<0>(subtree[0]);

            std::string devName =
                pcie_util::buildPCIeUniquePath(pcieDevciePath);

            if (devName.empty())
            {
                BMCWEB_LOG_ERROR << "Failed to find / in pcie device path";
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res.jsonValue["Slots"][index]["Links"]["PCIeDevice"] = {
                {{"@odata.id",
                  "/redfish/v1/Systems/system/PCIeDevices/" + devName}}};
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", slotPath, 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.PCIeDevice"});
}

inline void
    linkAssociatedProcessor(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& pcieSlotPath, size_t index)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, pcieSlotPath,
         index](const boost::system::error_code ec,
                const std::variant<std::vector<std::string>>& endpoints) {
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    // This PCIeSlot have no processor association.
                    BMCWEB_LOG_DEBUG << "No processor association found";
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
                BMCWEB_LOG_DEBUG << "Error getting processor association!";
                messages::internalError(asyncResp->res);
                return;
            }

            if ((*value).size() < 1)
            {
                BMCWEB_LOG_DEBUG << "No association found for processor";
                messages::internalError(asyncResp->res);
                return;
            }

            const std::vector<std::string>& processorPath = *value;
            std::string cpuName =
                sdbusplus::message::object_path(processorPath[0]).filename();
            sdbusplus::message::object_path parentPath =
                sdbusplus::message::object_path(processorPath[0]).parent_path();
            std::string dcmName = parentPath.filename();
            std::string processorName = dcmName + '-' + cpuName;

            nlohmann::json& processorArray =
                asyncResp->res.jsonValue["Slots"][index]["Links"]["Processors"];
            processorArray = nlohmann::json::array();

            processorArray.push_back(
                {{"@odata.id",
                  "/redfish/v1/Systems/system/Processors/" + processorName}});
            asyncResp->res
                .jsonValue["Slots"][index]["Links"]["Processors@odata.count"] =
                processorArray.size();
        },
        "xyz.openbmc_project.ObjectMapper",
        pcieSlotPath + "/upstream_processor", "org.freedesktop.DBus.Properties",
        "Get", "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Add PCIeSlot to NMVe backplane assembly link
 *
 * @param[in, out]  asyncResp       Async HTTP response.
 * @param[in]       pcieSlotPath    Object path of the PCIeSlot.
 * @param[in]       index           Index.
 */
inline void linkAssociatedDiskBackplane(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& pcieSlotPath, size_t index)
{
    auto respHandler =
        [asyncResp, pcieSlotPath,
         index](const boost::system::error_code ec,
                const std::variant<std::vector<std::string>>& endpoints) {
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    // Disk backplane association not found for this pcie slot.
                    BMCWEB_LOG_DEBUG << "Disk backplane association not found";
                    return;
                }
                BMCWEB_LOG_ERROR << "DBUS response error " << ec.message();
                messages::internalError(asyncResp->res);
                return;
            }

            const std::vector<std::string>* value =
                std::get_if<std::vector<std::string>>(&endpoints);

            if (value == nullptr)
            {
                BMCWEB_LOG_DEBUG
                    << "Error getting disk backplane drives association!";
                messages::internalError(asyncResp->res);
                return;
            }

            if ((*value).size() <= 0)
            {
                BMCWEB_LOG_DEBUG
                    << "No association was found for disk backplane drive";
                messages::internalError(asyncResp->res);
                return;
            }

            // Each slot points to one disk backplane, so picking the top one
            // or the only one we will have instead of looping through.
            const std::string drivePath = (*value)[0];
            const std::string chassisId = "chassis";

            auto backplaneAssemblyCallback =
                [asyncResp, index, chassisId,
                 drivePath](const std::vector<std::string>& assemblyList) {
                    auto it = std::find(assemblyList.begin(),
                                        assemblyList.end(), drivePath);
                    if (it != assemblyList.end())
                    {
                        asyncResp->res.jsonValue["Slots"][index]["Links"]["Oem"]
                                                ["IBM"]["AssociatedAssembly"] =
                            {{{"@odata.id",
                               "/redfish/v1/Chassis/" + chassisId +
                                   "/Assembly#/Assemblies/" +
                                   std::to_string(it - assemblyList.begin())}}};
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR << "Drive path " << drivePath
                                         << "not found in the assembly list";
                        messages::internalError(asyncResp->res);
                    }
                };

            redfish::chassis_utils::getChassisAssembly(
                asyncResp, chassisId, std::move(backplaneAssemblyCallback));
        };

    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.ObjectMapper",
        pcieSlotPath + "/inventory", "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    addLinkedFabricAdapter(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& pcieSlotPath, size_t index)
{
    constexpr std::array<const char*, 1> fabricAdapterInterfaces{
        "xyz.openbmc_project.Inventory.Item.FabricAdapter"};
    dbus::utility::getAssociatedSubTreePaths(
        pcieSlotPath + "/contained_by",
        sdbusplus::message::object_path("/xyz/openbmc_project/inventory"), 0,
        fabricAdapterInterfaces,
        [asyncResp, pcieSlotPath,
         index](const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreePathsResponse&
                    fabricAdapterPaths) {
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    BMCWEB_LOG_DEBUG
                        << "FabricAdapter Slot association not found";
                    return;
                }
                BMCWEB_LOG_ERROR << "DBUS response error " << ec.value();
                messages::internalError(asyncResp->res);
                return;
            }
            if (fabricAdapterPaths.empty())
            {
                // No association to FabricAdapter
                BMCWEB_LOG_DEBUG << "FabricAdapter Slot association not found";
                return;
            }
            if (fabricAdapterPaths.size() > 1)
            {
                BMCWEB_LOG_ERROR
                    << "DBUS response has more than FabricAdapters of "
                    << fabricAdapterPaths.size();
                messages::internalError(asyncResp->res);
                return;
            }

            // Add a link to FabricAdapter
            const std::string& fabricAdapterPath = fabricAdapterPaths.front();
            nlohmann::json& slot = asyncResp->res.jsonValue["Slots"][index];

            slot["Oem"]["@odata.type"] = "#OemPCIeSlots.Oem";
            slot["Oem"]["IBM"]["@odata.type"] = "#OemPCIeSlots.IBM";
            slot["Oem"]["IBM"]["UpstreamFabricAdapter"]["@odata.id"] =
                "/redfish/v1/Systems/system/FabricAdapters/" +
                fabric_util::buildFabricUniquePath(fabricAdapterPath);
        });
}

inline void getPCIeSlots(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisID)
{
    BMCWEB_LOG_DEBUG << "Get properties for PCIeSlots associated to chassis = "
                     << chassisID;

    asyncResp->res.jsonValue = {{"@odata.type", "#PCIeSlots.v1_5_0.PCIeSlots"},
                                {"Name", "PCIe Slot Information"}};
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisID + "/PCIeSlots";
    asyncResp->res.jsonValue["Id"] = "PCIeSlots";
    asyncResp->res.jsonValue["Slots"] = nlohmann::json::array();

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            if (subtree.size() == 0)
            {
                BMCWEB_LOG_ERROR << "Can't find PCIeSlot D-Bus object!";
                return;
            }

            for (const auto& [objectPath, serviceName] : subtree)
            {
                if (objectPath.empty() || serviceName.size() != 1)
                {
                    BMCWEB_LOG_ERROR << "Error getting PCIeSlot D-Bus object!";
                    messages::internalError(asyncResp->res);
                    return;
                }

                const std::string& connectionName = serviceName[0].first;
                const std::vector<std::string>& interfaceList =
                    serviceName[0].second;
                const std::string pcieSlotPath = objectPath;

                // The association of this PCIeSlot is used to determine whether
                // it belongs to this ChassisID
                crow::connections::systemBus->async_method_call(
                    [asyncResp, chassisID, pcieSlotPath, connectionName,
                     interfaceList](
                        const boost::system::error_code ec,
                        const std::variant<std::vector<std::string>>&
                            endpoints) {
                        if (ec)
                        {
                            if (ec.value() == EBADR)
                            {
                                // This PCIeSlot have no chassis association.
                                return;
                            }
                            BMCWEB_LOG_ERROR << "DBUS response error";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        const std::vector<std::string>* pcieSlotChassis =
                            std::get_if<std::vector<std::string>>(&(endpoints));

                        if (pcieSlotChassis == nullptr)
                        {
                            BMCWEB_LOG_ERROR
                                << "Error getting PCIe Slot association!";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        if ((*pcieSlotChassis).size() != 1)
                        {
                            BMCWEB_LOG_ERROR << "PCIe Slot association error! ";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        std::vector<std::string> chassisPath = *pcieSlotChassis;
                        sdbusplus::message::object_path path(chassisPath[0]);
                        std::string chassisName = path.filename();
                        if (chassisName != chassisID)
                        {
                            // The pcie slot does't belong to the chassisID
                            return;
                        }

                        crow::connections::systemBus->async_method_call(
                            [asyncResp, pcieSlotPath, connectionName,
                             interfaceList](
                                const boost::system::error_code ec,
                                const std::vector<std::pair<
                                    std::string,
                                    std::variant<bool, size_t, std::string>>>&
                                    propertiesList) {
                                if (ec)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "Can't get PCIeSlot properties!";
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                nlohmann::json& tempArray =
                                    asyncResp->res.jsonValue["Slots"];
                                size_t index = tempArray.size();
                                tempArray.push_back({});
                                nlohmann::json& propertyData = tempArray.back();

                                // Get pcie slot location indicator state
                                getLocationIndicatorActive(
                                    asyncResp, pcieSlotPath, propertyData);

                                for (const auto& property : propertiesList)
                                {
                                    const std::string& propertyName =
                                        property.first;

                                    if (propertyName == "Generation")
                                    {
                                        const std::string* value =
                                            std::get_if<std::string>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        std::string pcieType =
                                            analysisGeneration(*value);
                                        if (!pcieType.empty())
                                        {
                                            propertyData["PCIeType"] = pcieType;
                                        }
                                    }
                                    else if (propertyName == "Lanes")
                                    {
                                        const size_t* value =
                                            std::get_if<size_t>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        propertyData["Lanes"] = *value;
                                    }
                                    else if (propertyName == "SlotType")
                                    {
                                        const std::string* value =
                                            std::get_if<std::string>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        std::string slotType =
                                            analysisSlotType(*value);
                                        if (!slotType.empty())
                                        {
                                            propertyData["SlotType"] = slotType;
                                        }
                                    }
                                    else if (propertyName == "HotPluggable")
                                    {
                                        const bool* value =
                                            std::get_if<bool>(&property.second);
                                        if (value == nullptr)
                                        {
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        propertyData["HotPluggable"] = *value;
                                    }
                                    else if (propertyName == "BusId")
                                    {
                                        const size_t* value =
                                            std::get_if<size_t>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        propertyData["Oem"]["@odata.type"] =
                                            "#OemPCIeSlots.Oem";
                                        propertyData["Oem"]["IBM"]
                                                    ["@odata.type"] =
                                                        "#OemPCIeSlots.IBM";
                                        propertyData["Oem"]["IBM"]["LinkId"] =
                                            *value;
                                    }
                                }

                                const std::string locationInterface =
                                    "xyz.openbmc_project.Inventory.Decorator."
                                    "LocationCode";
                                if (std::find(interfaceList.begin(),
                                              interfaceList.end(),
                                              locationInterface) !=
                                    interfaceList.end())
                                {
                                    crow::connections::systemBus
                                        ->async_method_call(
                                            [asyncResp, index](
                                                const boost::system::error_code
                                                    ec,
                                                const std::variant<std::string>&
                                                    property) {
                                                if (ec)
                                                {
                                                    BMCWEB_LOG_DEBUG
                                                        << "DBUS response "
                                                           "error";
                                                    messages::internalError(
                                                        asyncResp->res);
                                                    return;
                                                }

                                                const std::string* value =
                                                    std::get_if<std::string>(
                                                        &property);

                                                if (value == nullptr)
                                                {
                                                    messages::internalError(
                                                        asyncResp->res);
                                                    return;
                                                }
                                                asyncResp->res
                                                    .jsonValue["Slots"][index]
                                                              ["Location"]
                                                              ["PartLocation"]
                                                              ["ServiceLabel"] =
                                                    *value;
                                            },
                                            connectionName, pcieSlotPath,
                                            "org.freedesktop.DBus.Properties",
                                            "Get", locationInterface,
                                            "LocationCode");
                                }
                                addLinkedPcieDevices(asyncResp, pcieSlotPath,
                                                     index);

                                // Get FabricAdapter device link if exists
                                addLinkedFabricAdapter(asyncResp, pcieSlotPath,
                                                       index);

                                linkAssociatedProcessor(asyncResp, pcieSlotPath,
                                                        index);
                                linkAssociatedDiskBackplane(
                                    asyncResp, pcieSlotPath, index);
                            },
                            connectionName, pcieSlotPath,
                            "org.freedesktop.DBus.Properties", "GetAll",
                            "xyz.openbmc_project.Inventory.Item.PCIeSlot");
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    pcieSlotPath + "/chassis",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.PCIeSlot"});
}

// We need a global variable to keep track of the actual number of slots,
// and then use this variable to check if the number of slots in the request is
// correct
unsigned int index = 0;

// Check whether the total number of slots in the request is consistent with the
// actual total number of slots
template <typename Callback>
void checkPCIeSlotsCount(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisID, const unsigned int total,
                         Callback&& callback)
{
    BMCWEB_LOG_DEBUG << "Check PCIeSlots count for PCIeSlots request "
                        "associated to chassis = "
                     << chassisID;

    // Count of slots that do not belong to the chassis represented by
    // chassisID
    static size_t slotsNotInChassis = 0;

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, total, callback{std::move(callback)}](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            if (subtree.size() == 0)
            {
                BMCWEB_LOG_ERROR << "Can't find PCIeSlot D-Bus object!";
                return;
            }

            index = 0;
            slotsNotInChassis = 0;
            auto slotNum = subtree.size();
            unsigned int slotCheckIndex = 0;
            for (const auto& [objectPath, serviceName] : subtree)
            {
                slotCheckIndex++;

                if (objectPath.empty() || serviceName.size() != 1)
                {
                    BMCWEB_LOG_ERROR << "Error getting PCIeSlot D-Bus object!";
                    messages::internalError(asyncResp->res);
                    return;
                }

                const std::string pcieSlotPath = objectPath;

                // The association of this PCIeSlot is used to determine
                // whether it belongs to this ChassisID
                crow::connections::systemBus->async_method_call(
                    [asyncResp, chassisID, pcieSlotPath, total, slotNum,
                     slotCheckIndex, callback{std::move(callback)}](
                        const boost::system::error_code ec,
                        const std::variant<std::vector<std::string>>&
                            endpoints) {
                        if (ec)
                        {
                            if (ec.value() == EBADR)
                            {
                                if (slotCheckIndex == slotNum)
                                {
                                    // the total number of slots in the request
                                    // is correct
                                    if (index == total)
                                    {
                                        callback();
                                    }
                                    else
                                    {
                                        messages::resourceNotFound(
                                            asyncResp->res, "Chassis",
                                            "slots count");
                                        return;
                                    }
                                }

                                // This PCIeSlot have no chassis association.
                                return;
                            }

                            BMCWEB_LOG_ERROR << "DBUS response error";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        const std::vector<std::string>* pcieSlotChassis =
                            std::get_if<std::vector<std::string>>(&(endpoints));

                        if (pcieSlotChassis == nullptr)
                        {
                            return;
                        }

                        if ((*pcieSlotChassis).size() != 1)
                        {
                            BMCWEB_LOG_ERROR << "PCIe Slot association error! ";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        std::vector<std::string> chassisPath = *pcieSlotChassis;
                        sdbusplus::message::object_path path(chassisPath[0]);
                        std::string chassisName = path.filename();
                        if (chassisName != chassisID)
                        {
                            slotsNotInChassis++;
                        }

                        index++;

                        // All the objectPaths have been checked
                        if (slotCheckIndex == slotNum)
                        {
                            // the total number of slots in the request is
                            // correct
                            if (index - slotsNotInChassis == total)
                            {
                                callback();
                            }
                            else
                            {
                                messages::resourceNotFound(
                                    asyncResp->res, "Chassis", "slots count");
                                return;
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    pcieSlotPath + "/chassis",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.PCIeSlot"});
}
inline void setPCIeSlotsLocationIndicator(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID,
    const std::map<unsigned int, bool>& locationIndicatorActiveMap)
{
    BMCWEB_LOG_DEBUG
        << "Set locationIndicatorActive for PCIeSlots associated to chassis = "
        << chassisID;

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, locationIndicatorActiveMap](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            if (subtree.size() == 0)
            {
                BMCWEB_LOG_ERROR << "Can't find PCIeSlot D-Bus object!";
                return;
            }

            index = 0;
            for (const auto& [objectPath, serviceName] : subtree)
            {
                if (objectPath.empty() || serviceName.size() != 1)
                {
                    BMCWEB_LOG_ERROR << "Error getting PCIeSlot D-Bus object!";
                    messages::internalError(asyncResp->res);
                    return;
                }

                const std::string pcieSlotPath = objectPath;

                // The association of this PCIeSlot is used to determine whether
                // it belongs to this ChassisID
                crow::connections::systemBus->async_method_call(
                    [asyncResp, chassisID, pcieSlotPath,
                     locationIndicatorActiveMap](
                        const boost::system::error_code ec,
                        const std::variant<std::vector<std::string>>&
                            endpoints) {
                        if (ec)
                        {
                            if (ec.value() == EBADR)
                            {
                                // This PCIeSlot have no chassis association.
                                return;
                            }

                            BMCWEB_LOG_ERROR << "DBUS response error";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        const std::vector<std::string>* pcieSlotChassis =
                            std::get_if<std::vector<std::string>>(&(endpoints));

                        if (pcieSlotChassis == nullptr)
                        {
                            return;
                        }

                        if ((*pcieSlotChassis).size() != 1)
                        {
                            BMCWEB_LOG_ERROR << "PCIe Slot association error! ";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        std::vector<std::string> chassisPath = *pcieSlotChassis;
                        sdbusplus::message::object_path path(chassisPath[0]);
                        std::string chassisName = path.filename();
                        if (chassisName != chassisID)
                        {
                            // The pcie slot does't belong to the chassisID
                            return;
                        }

                        index++;
                        // This index is consistent with the index in the
                        // request, so use this index to find the value
                        auto iter = locationIndicatorActiveMap.find(index);

                        if (iter != locationIndicatorActiveMap.end())
                        {
                            setLocationIndicatorActive(asyncResp, pcieSlotPath,
                                                       iter->second);
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    pcieSlotPath + "/chassis",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.PCIeSlot"});
}

inline void requestRoutesPCIeSlots(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PCIeSlots/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisID) {
                auto getChassisID =
                    [asyncResp, chassisID](
                        const std::optional<std::string>& validChassisID) {
                        if (!validChassisID)
                        {
                            BMCWEB_LOG_ERROR << "Not a valid chassis ID:"
                                             << chassisID;
                            messages::resourceNotFound(asyncResp->res,
                                                       "Chassis", chassisID);
                            return;
                        }

                        getPCIeSlots(asyncResp, *validChassisID);
                    };
                redfish::chassis_utils::getValidChassisID(
                    asyncResp, chassisID, std::move(getChassisID));
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PCIeSlots/")
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::patch)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisID) {
                std::optional<std::vector<nlohmann::json>> slotsData;
                if (!json_util::readJson(req, asyncResp->res, "Slots",
                                         slotsData))
                {
                    return;
                }
                if (!slotsData)
                {
                    return;
                }
                std::vector<nlohmann::json> slots = std::move(*slotsData);
                std::map<unsigned int, bool> locationIndicatorActiveMap;
                unsigned int slotIndex = 0;
                unsigned int total = 0;
                for (auto& slot : slots)
                {
                    slotIndex++;

                    if (slot.empty())
                    {
                        continue;
                    }

                    bool locationIndicatorActive;
                    if (json_util::readJson(slot, asyncResp->res,
                                            "LocationIndicatorActive",
                                            locationIndicatorActive))
                    {
                        locationIndicatorActiveMap[slotIndex] =
                            locationIndicatorActive;
                    }
                }

                total = slotIndex;
                if (total == 0)
                {
                    return;
                }

                auto getChassisID =
                    [asyncResp, chassisID, total,
                     locationIndicatorActiveMap{
                         std::move(locationIndicatorActiveMap)}](
                        const std::optional<std::string>& validChassisID) {
                        if (!validChassisID)
                        {
                            BMCWEB_LOG_ERROR << "Not a valid chassis ID:"
                                             << chassisID;
                            messages::resourceNotFound(asyncResp->res,
                                                       "Chassis", chassisID);
                            return;
                        }
                        auto checkPCIeSlots = [asyncResp, chassisID,
                                               locationIndicatorActiveMap] {
                            setPCIeSlotsLocationIndicator(
                                asyncResp, chassisID,
                                locationIndicatorActiveMap);
                        };
                        // Get the correct Path and Service that match the input
                        // parameters
                        checkPCIeSlotsCount(asyncResp, chassisID, total,
                                            std::move(checkPCIeSlots));
                    };
                redfish::chassis_utils::getValidChassisID(
                    asyncResp, chassisID, std::move(getChassisID));
            });
}

} // namespace redfish
