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
#include <utils/json_utils.hpp>
#include <utils/pcie_util.hpp>

#include <memory>
#include <string>
#include <variant>
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
            auto it =
                std::find(assemblyList.begin(), assemblyList.end(), drivePath);
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

// We need a global variable to keep track of the actual number of slots,
// and then use this variable to check if the number of slots in the request
// is correct
std::map<unsigned int, std::string> updatePcieSlotsMaps{};

// Check whether the total number of slots in the request is consistent with the
// actual total number of slots
void checkPCIeSlotsCount(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const unsigned int total, const std::string& validChassisPath,
    std::function<void(const std::map<unsigned int, std::string>&)>&& callback)
{
    BMCWEB_LOG_DEBUG << "Check PCIeSlots count for PCIeSlots request "
                        "associated to chassis";

    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.PCIeSlot"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, total, validChassisPath, callback{std::move(callback)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse&
                pcieSlotsPaths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree " << ec;
            messages::internalError(asyncResp->res);
            return;
        }

        unsigned int index = 0;
        unsigned int count = 0;
        auto slotNum = pcieSlotsPaths.size();
        for (const auto& path : pcieSlotsPaths)
        {
            index++;
            dbus::utility::getAssociationEndPoints(
                path + "/chassis",
                [asyncResp, total, validChassisPath, count{++count}, slotNum,
                 index, path,
                 callback](const boost::system::error_code& ec1,
                           const dbus::utility::MapperEndPoints& endpoints) {
                if (ec1)
                {
                    if (ec1.value() == EBADR)
                    {
                        if (count == slotNum)
                        {
                            // the total number of slots in the request
                            // is correct
                            if (updatePcieSlotsMaps.size() == total)
                            {
                                callback(updatePcieSlotsMaps);
                            }
                            else
                            {
                                messages::resourceNotFound(
                                    asyncResp->res, "Chassis", "slots count");
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

                for (const auto& endpoint : endpoints)
                {
                    if (endpoint == validChassisPath)
                    {
                        updatePcieSlotsMaps[index] = path;
                    }
                }

                if (updatePcieSlotsMaps.size() > total)
                {
                    BMCWEB_LOG_ERROR
                        << "The actual number of cpieslots is greater than total";
                    messages::internalError(asyncResp->res);
                }

                if (count == slotNum)
                {
                    // Last time DBus has returned
                    if (updatePcieSlotsMaps.size() == total)
                    {
                        callback(updatePcieSlotsMaps);
                    }
                    else
                    {
                        messages::internalError(asyncResp->res);
                    }
                }
                });
        }
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
    onPcieSlotGetAllDone(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const boost::system::error_code ec,
                         const dbus::utility::DBusPropertiesMap& propertiesList,
                         const std::string& connectionName,
                         const std::string& pcieSlotPath)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR << "Can't get PCIeSlot properties!";
        messages::internalError(asyncResp->res);
        return;
    }

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

    // Get processor link
    linkAssociatedProcessor(asyncResp, pcieSlotPath, index);

    linkAssociatedDiskBackplane(asyncResp, pcieSlotPath, index);

    // Get pcie slot location indicator state
    nlohmann::json& slotLIA = slots.back();
    getLocationIndicatorActive(asyncResp, pcieSlotPath, slotLIA);
}

inline void onMapperAssociationDone(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& pcieSlotPath,
    const std::string& connectionName, const boost::system::error_code& ec,
    const std::variant<std::vector<std::string>>& endpoints)
{
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
        BMCWEB_LOG_ERROR << "Error getting PCIe Slot association!";
        messages::internalError(asyncResp->res);
        return;
    }

    if (pcieSlotChassis->size() != 1)
    {
        BMCWEB_LOG_ERROR << "PCIe Slot association error! ";
        messages::internalError(asyncResp->res);
        return;
    }

    sdbusplus::message::object_path path((*pcieSlotChassis)[0]);
    std::string chassisName = path.filename();
    if (chassisName != chassisID)
    {
        // The pcie slot doesn't belong to the chassisID
        return;
    }

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, connectionName, pcieSlotPath,
        "xyz.openbmc_project.Inventory.Item.PCIeSlot",
        [asyncResp, connectionName,
         pcieSlotPath](const boost::system::error_code& ec1,
                       const dbus::utility::DBusPropertiesMap& propertiesList) {
        onPcieSlotGetAllDone(asyncResp, ec1, propertiesList, connectionName,
                             pcieSlotPath);
        });
}

inline void
    onMapperSubtreeDone(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisID,
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree " << ec;
        messages::internalError(asyncResp->res);
        return;
    }
    if (subtree.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisID);
        return;
    }

    BMCWEB_LOG_DEBUG << "Get properties for PCIeSlots associated to chassis = "
                     << chassisID;

    asyncResp->res.jsonValue["@odata.type"] = "#PCIeSlots.v1_5_0.PCIeSlots";
    asyncResp->res.jsonValue["Name"] = "PCIe Slot Information";
    asyncResp->res.jsonValue["@odata.id"] = crow::utility::urlFromPieces(
        "redfish", "v1", "Chassis", chassisID, "PCIeSlots");
    asyncResp->res.jsonValue["Id"] = "1";
    asyncResp->res.jsonValue["Slots"] = nlohmann::json::array();

    for (const auto& pathServicePair : subtree)
    {
        const std::string& pcieSlotPath = pathServicePair.first;
        for (const auto& connectionInterfacePair : pathServicePair.second)
        {
            const std::string& connectionName = connectionInterfacePair.first;
            sdbusplus::message::object_path pcieSlotAssociationPath(
                pcieSlotPath);
            pcieSlotAssociationPath /= "chassis";

            // The association of this PCIeSlot is used to determine whether
            // it belongs to this ChassisID
            dbus::utility::getAssociationEndPoints(
                pcieSlotAssociationPath,
                [asyncResp, chassisID, pcieSlotPath, connectionName](
                    const boost::system::error_code& ec1,
                    const dbus::utility::MapperEndPoints& endpoints) {
                onMapperAssociationDone(asyncResp, chassisID, pcieSlotPath,
                                        connectionName, ec1, endpoints);
                });
        }
    }
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

    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Inventory.Item.PCIeSlot"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp,
         chassisID](const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        onMapperSubtreeDone(asyncResp, chassisID, ec, subtree);
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

    // Clear updatePcieSlotsMaps
    updatePcieSlotsMaps.clear();

    unsigned int total = static_cast<unsigned int>(slots.size());
    auto respHandler =
        [asyncResp, chassisId, total,
         locationIndicatorActiveMap{std::move(locationIndicatorActiveMap)}](
            const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            BMCWEB_LOG_ERROR << "Not a valid chassis ID:" << chassisId;
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }

        // Get the correct Path and Service that match the input
        // parameters
        checkPCIeSlotsCount(
            asyncResp, total, *validChassisPath,
            [asyncResp, locationIndicatorActiveMap](
                const std::map<unsigned int, std::string>& maps) {
            for (const auto& [i, v] : locationIndicatorActiveMap)
            {
                if (maps.contains(i))
                {
                    setLocationIndicatorActive(asyncResp, maps.at(i), v);
                }
            }
            });
    };

    redfish::chassis_utils::getValidChassisPath(asyncResp, chassisId,
                                                std::bind_front(respHandler));
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
