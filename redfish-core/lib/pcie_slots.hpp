#pragma once

#include "error_messages.hpp"
#include "generated/enums/pcie_slots.hpp"
#include "led.hpp"
#include "utility.hpp"

#include <app.hpp>
#include <boost/system/error_code.hpp>
#include <dbus_utility.hpp>
#include <pcie.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/fabric_util.hpp>
#include <utils/json_utils.hpp>
#include <utils/pcie_util.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace redfish
{

inline std::optional<pcie_slots::SlotTypes>
    dbusSlotTypeToRf(const std::string& slotType)
{
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.FullLength")
    {
        return pcie_slots::SlotTypes::FullLength;
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.HalfLength")
    {
        return pcie_slots::SlotTypes::HalfLength;
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.LowProfile")
    {
        return pcie_slots::SlotTypes::LowProfile;
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.Mini")
    {
        return pcie_slots::SlotTypes::Mini;
    }
    if (slotType == "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.M_2")
    {
        return pcie_slots::SlotTypes::M2;
    }
    if (slotType == "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.OEM")
    {
        return pcie_slots::SlotTypes::OEM;
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.OCP3Small")
    {
        return pcie_slots::SlotTypes::OCP3Small;
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.OCP3Large")
    {
        return pcie_slots::SlotTypes::OCP3Large;
    }
    if (slotType == "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.U_2")
    {
        return pcie_slots::SlotTypes::U2;
    }
    if (slotType.empty() ||
        slotType ==
            "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.Unknown")
    {
        return pcie_slots::SlotTypes::Invalid;
    }

    // Unspecified slotType needs return an internal error.
    return std::nullopt;
}

inline void
    addLinkedPcieDevices(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& slotPath, size_t index)
{
    // Collect device associated with this slot and
    // populate it here
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.PCIeDevice"};
    dbus::utility::getSubTree(
        slotPath, 0, interfaces,
        [asyncResp,
         index](const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree " << ec;
            messages::internalError(asyncResp->res);
            return;
        }
        if (subtree.empty())
        {
            BMCWEB_LOG_DEBUG
                << "Can't find PCIeDevice D-Bus object for given slot";
            return;
        }

        // Assuming only one device path per slot.
        const std::string& pcieDevciePath = std::get<0>(subtree[0]);

        std::string devName = pcie_util::buildPCIeUniquePath(pcieDevciePath);

        if (devName.empty())
        {
            BMCWEB_LOG_ERROR << "Failed to find / in pcie device path";
            messages::internalError(asyncResp->res);
            return;
        }

        asyncResp->res.jsonValue["Slots"][index]["Links"]["PCIeDevice"] = {
            {{"@odata.id",
              "/redfish/v1/Systems/system/PCIeDevices/" + devName}}};
        });
}

inline void
    linkAssociatedProcessor(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& pcieSlotPath, size_t index)
{
    dbus::utility::getAssociationEndPoints(
        pcieSlotPath + "/upstream_processor",
        [asyncResp, pcieSlotPath,
         index](const boost::system::error_code& ec,
                const dbus::utility::MapperEndPoints& endpoints) {
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

        if (endpoints.empty())
        {
            BMCWEB_LOG_DEBUG << "No association found for processor";
            messages::internalError(asyncResp->res);
            return;
        }

        std::string cpuName =
            sdbusplus::message::object_path(endpoints[0]).filename();
        std::string dcmName =
            (sdbusplus::message::object_path(endpoints[0]).parent_path())
                .filename();

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
        });
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
    dbus::utility::getAssociationEndPoints(
        pcieSlotPath + "/inventory",
        [asyncResp, pcieSlotPath,
         index](const boost::system::error_code& ec,
                const dbus::utility::MapperEndPoints& endpoints) {
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

        if (endpoints.empty())
        {
            BMCWEB_LOG_DEBUG
                << "No association was found for disk backplane drive";
            messages::internalError(asyncResp->res);
            return;
        }

        // Each slot points to one disk backplane, so picking the top one
        // or the only one we will have instead of looping through.
        const std::string& drivePath = endpoints[0];
        const std::string& chassisId = "chassis";

        auto backplaneAssemblyCallback =
            [asyncResp, index, chassisId,
             drivePath](const std::vector<std::string>& assemblyList) {
            auto it = std::find(assemblyList.begin(), assemblyList.end(),
                                drivePath);
            if (it != assemblyList.end())
            {
                asyncResp->res.jsonValue["Slots"][index]["Links"]["Oem"]["IBM"]
                                        ["AssociatedAssembly"] = {
                    {{"@odata.id",
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
        });
}

inline void getLocationCode(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const size_t index,
                            const std::string& connectionName,
                            const std::string& pcieSlotPath)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, pcieSlotPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp, index](const boost::system::error_code& ec1,
                           const std::string& property) {
        if (ec1)
        {
            if (ec1.value() == EBADR)
            {
                // Don't always have PCIeSlot location codes
                BMCWEB_LOG_DEBUG << "No slot location code found";
                return;
            }

            BMCWEB_LOG_ERROR << "Can't get location code property for PCIeSlot";
            messages::internalError(asyncResp->res);
            return;
        }
        if (property.empty())
        {
            BMCWEB_LOG_WARNING << "PcieSlot location code value is empty ";
            return;
        }
        asyncResp->res.jsonValue["Slots"][index]["Location"]["PartLocation"]
                                ["ServiceLabel"] = property;
        });
}

inline void
    addLinkedFabricAdapter(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& pcieSlotPath, size_t index)
{
    constexpr std::array<std::string_view, 1> fabricAdapterInterfaces{
        "xyz.openbmc_project.Inventory.Item.FabricAdapter"};
    dbus::utility::getAssociatedSubTreePaths(
        pcieSlotPath + "/contained_by",
        sdbusplus::message::object_path("/xyz/openbmc_project/inventory"), 0,
        fabricAdapterInterfaces,
        [asyncResp, pcieSlotPath,
         index](const boost::system::error_code& ec,
                const dbus::utility::MapperEndPoints& fabricAdapterPaths) {
        if (ec)
        {
            if (ec.value() == EBADR)
            {
                BMCWEB_LOG_DEBUG << "FabricAdapter Slot association not found";
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
            BMCWEB_LOG_ERROR << "DBUS response has more than FabricAdapters of "
                             << fabricAdapterPaths.size();
            messages::internalError(asyncResp->res);
            return;
        }

        // Add a link to FabricAdapter
        const std::string& fabricAdapterPath = fabricAdapterPaths.front();
        nlohmann::json& slot = asyncResp->res.jsonValue["Slots"][index];

        slot["Links"]["Oem"]["@odata.type"] = "#OemPCIeSlots.Oem";
        slot["Links"]["Oem"]["IBM"]["@odata.type"] = "#OemPCIeSlots.IBM";
        slot["Links"]["Oem"]["IBM"]["UpstreamFabricAdapter"]["@odata.id"] =
            crow::utility::urlFromPieces(
                "redfish", "v1", "Systems", "system", "FabricAdapters",
                fabric_util::buildFabricUniquePath(fabricAdapterPath));
        });
}

inline void getPCIeSlotProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const dbus::utility::DBusPropertiesMap& propertiesList,
    const std::string& connectionName, const std::string& pcieSlotPath)
{
    nlohmann::json& slots = asyncResp->res.jsonValue["Slots"];

    nlohmann::json::array_t* slotsPtr =
        slots.get_ptr<nlohmann::json::array_t*>();
    if (slotsPtr == nullptr)
    {
        BMCWEB_LOG_ERROR << "Slots key isn't an array???";
        messages::internalError(asyncResp->res);
        return;
    }

    nlohmann::json::object_t slot;

    const std::string* generation = nullptr;
    const size_t* lanes = nullptr;
    const std::string* slotType = nullptr;
    const bool* hotPluggable = nullptr;
    const size_t* busId = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "Generation",
        generation, "Lanes", lanes, "SlotType", slotType, "HotPluggable",
        hotPluggable, "BusId", busId);

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    if (generation != nullptr)
    {
        std::optional<pcie_device::PCIeTypes> pcieType =
            redfishPcieGenerationFromDbus(*generation);
        if (!pcieType)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        if (*pcieType != pcie_device::PCIeTypes::Invalid)
        {
            slot["PCIeType"] = *pcieType;
        }
    }

    if (lanes != nullptr)
    {
        slot["Lanes"] = *lanes;
    }

    if (slotType != nullptr)
    {
        std::optional<pcie_slots::SlotTypes> redfishSlotType =
            dbusSlotTypeToRf(*slotType);
        if (!redfishSlotType)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        if (*redfishSlotType != pcie_slots::SlotTypes::Invalid)
        {
            slot["SlotType"] = *redfishSlotType;
        }
    }

    if (hotPluggable != nullptr)
    {
        slot["HotPluggable"] = *hotPluggable;
    }

    if (busId != nullptr)
    {
        slot["Oem"]["@odata.type"] = "#OemPCIeSlots.Oem";
        slot["Oem"]["IBM"]["@odata.type"] = "#OemPCIeSlots.IBM";
        slot["Oem"]["IBM"]["LinkId"] = *busId;
    }

    size_t index = slots.size();
    slots.emplace_back(std::move(slot));

    // Get and set the location code
    getLocationCode(asyncResp, index, connectionName, pcieSlotPath);

    // Get pcie device link
    addLinkedPcieDevices(asyncResp, pcieSlotPath, index);

    // Get FabricAdapter device link if exists
    addLinkedFabricAdapter(asyncResp, pcieSlotPath, index);

    // Get processor link
    linkAssociatedProcessor(asyncResp, pcieSlotPath, index);

    linkAssociatedDiskBackplane(asyncResp, pcieSlotPath, index);

    // Get pcie slot location indicator state
    nlohmann::json& slotLIA = slots.back();
    getLocationIndicatorActive(asyncResp, pcieSlotPath, slotLIA);
}

// Get all valid  PCIe Slots which are on the given chassis
inline void getValidPCIeSlotList(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& chassisPath,
    std::function<void(
        const std::vector<std::pair<std::string, std::string>>&)>&& callback)
{
    BMCWEB_LOG_DEBUG << "Get properties for PCIeSlots associated to chassis = "
                     << chassisID;

    // get PCIeSlots that are in chassis
    constexpr std::array<std::string_view, 1> pcieSlotIntf{
        "xyz.openbmc_project.Inventory.Item.PCIeSlot"};

    dbus::utility::getAssociatedSubTree(
        chassisPath + "/inventory",
        sdbusplus::message::object_path("/xyz/openbmc_project/inventory"), 0,
        pcieSlotIntf,
        [asyncResp, chassisID, chassisPath, callback{std::move(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree "
                             << ec.value();
            messages::internalError(asyncResp->res);
            return;
        }
        if (subtree.empty())
        {
            // No PCIeSlot found
            messages::resourceNotFound(asyncResp->res, "Chassis", "PCIeSlot");
            return;
        }

        std::vector<std::pair<std::string, std::string>> slotPathConnNames;
        for (const auto& [pcieSlotPath, serviceName] : subtree)
        {
            if (pcieSlotPath.empty() || serviceName.size() != 1)
            {
                BMCWEB_LOG_ERROR << "Error getting PCIeSlot D-Bus object!";
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string& connectionName = serviceName[0].first;
            slotPathConnNames.emplace_back(
                std::make_pair(pcieSlotPath, connectionName));
        }

        // sort by pcieSlotPath
        std::sort(slotPathConnNames.begin(), slotPathConnNames.end(),
                  [](const std::pair<std::string, std::string>& slot1,
                     const std::pair<std::string, std::string>& slot2) {
            return slot1.first < slot2.first;
        });

        callback(std::move(slotPathConnNames));
        });
}

inline void doHandlePCIeSlotCollectionGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& validChassisPath)
{
    getValidPCIeSlotList(
        asyncResp, chassisID, validChassisPath,
        [asyncResp,
         chassisID](const std::vector<std::pair<std::string, std::string>>&
                        slotPathConnNames) {
        asyncResp->res.jsonValue["@odata.type"] = "#PCIeSlots.v1_5_0.PCIeSlots";
        asyncResp->res.jsonValue["Name"] = "PCIe Slot Information";
        asyncResp->res.jsonValue["@odata.id"] = crow::utility::urlFromPieces(
            "redfish", "v1", "Chassis", chassisID, "PCIeSlots");
        asyncResp->res.jsonValue["Id"] = "1";
        asyncResp->res.jsonValue["Slots"] = nlohmann::json::array();

        for (const auto& [pcieSlotPath, connectionName] : slotPathConnNames)
        {
            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, connectionName, pcieSlotPath,
                "xyz.openbmc_project.Inventory.Item.PCIeSlot",
                [asyncResp, connectionName, pcieSlotPath](
                    const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR << "Can't get PCIeSlot properties! ec="
                                     << ec.value();
                    messages::internalError(asyncResp->res);
                    return;
                }
                getPCIeSlotProperties(asyncResp, propertiesList, connectionName,
                                      pcieSlotPath);
                });
        }
        });
}

inline void handlePCIeSlotCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisID,
        [asyncResp,
         chassisID](const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            BMCWEB_LOG_WARNING << "Not a valid chassis ID:" << chassisID;
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisID);
            return;
        }
        doHandlePCIeSlotCollectionGet(asyncResp, chassisID, *validChassisPath);
        });
}

inline void doHandlePCIeSlotPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, size_t total,
    const std::string& chassisID, const std::string& validChassisPath,
    const std::map<unsigned int, bool>& locationIndicatorActiveMap)
{
    getValidPCIeSlotList(
        asyncResp, chassisID, validChassisPath,
        [asyncResp, chassisID, total,
         locationIndicatorActiveMap{std::move(locationIndicatorActiveMap)}](
            const std::vector<std::pair<std::string, std::string>>&
                slotPathConnNames) {
        if (slotPathConnNames.size() != total)
        {
            BMCWEB_LOG_WARNING
                << "The actual number of pcieslots is different from the number of the input slots";
            int64_t total_count = static_cast<int64_t>(total);
            messages::invalidIndex(asyncResp->res, total_count);
        }

        unsigned int idx = 0;
        for (const auto& [pcieSlotPath, connectionName] : slotPathConnNames)
        {
            idx++;
            if (!locationIndicatorActiveMap.contains(idx))
            {
                continue;
            }

            auto indicatorOnOff = locationIndicatorActiveMap.at(idx);
            setLocationIndicatorActive(asyncResp, pcieSlotPath, indicatorOnOff);
        }
        });
}

inline void
    handlePCIeSlotsPatch(App& app, const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<std::vector<nlohmann::json>> slotsData;
    if (!json_util::readJsonPatch(req, asyncResp->res, "Slots", slotsData))
    {
        return;
    }
    if (!slotsData || slotsData->empty())
    {
        return;
    }
    std::vector<nlohmann::json> slots = std::move(*slotsData);
    std::map<unsigned int, bool> locationIndicatorActiveMap;
    unsigned int slotIndex = 0;
    for (auto& slot : slots)
    {
        slotIndex++;
        if (slot.empty())
        {
            continue;
        }

        bool locationIndicatorActive = false;
        if (json_util::readJson(slot, asyncResp->res, "LocationIndicatorActive",
                                locationIndicatorActive))
        {
            locationIndicatorActiveMap[slotIndex] = locationIndicatorActive;
        }
    }

    size_t total = slots.size();
    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp, chassisId, total,
         locationIndicatorActiveMap{std::move(locationIndicatorActiveMap)}](
            const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            BMCWEB_LOG_WARNING << "Not a valid chassis ID:" << chassisId;
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }
        doHandlePCIeSlotPatch(asyncResp, total, chassisId, *validChassisPath,
                              std::move(locationIndicatorActiveMap));
        });
}

inline void requestRoutesPCIeSlots(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PCIeSlots/")
        .privileges(redfish::privileges::getPCIeSlots)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePCIeSlotCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PCIeSlots/")
        .privileges(redfish::privileges::patchPCIeSlots)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handlePCIeSlotsPatch, std::ref(app)));
}

} // namespace redfish
