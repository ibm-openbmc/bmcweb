#pragma once

#include "error_messages.hpp"
#include "generated/enums/pcie_slots.hpp"
#include "led.hpp"
#include "utility.hpp"

#include <app.hpp>
#include <pcie.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>

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
        if (subtree.empty())
        {
            BMCWEB_LOG_DEBUG
                << "Can't find PCIeDevice D-Bus object for given slot";
            return;
        }

        // Assuming only one device path per slot.
        const std::string& pcieDevciePath = std::get<0>(subtree[0]);
        const std::string pcieDevice =
            sdbusplus::message::object_path(pcieDevciePath).filename();

        if (pcieDevice.empty())
        {
            BMCWEB_LOG_ERROR << "Failed to find / in pcie device path";
            messages::internalError(asyncResp->res);
            return;
        }

        asyncResp->res.jsonValue["Slots"][index]["Links"]["PCIeDevice"] = {
            {{"@odata.id",
              "/redfish/v1/Systems/system/PCIeDevices/" + pcieDevice}}};
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", slotPath, 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.PCIeDevice"});
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
    crow::connections::systemBus->async_method_call(
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
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.PCIeSlot"});
}

inline void
    onPcieSlotGetAllDone(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const boost::system::error_code ec,
                         const dbus::utility::DBusPropertiesMap& propertiesList,
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

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "Generation",
        generation, "Lanes", lanes, "SlotType", slotType, "HotPluggable",
        hotPluggable);

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

    size_t index = slots.size();
    slots.emplace_back(std::move(slot));

    // Get pcie device link
    addLinkedPcieDevices(asyncResp, pcieSlotPath, index);

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
        [asyncResp,
         pcieSlotPath](const boost::system::error_code& ec1,
                       const dbus::utility::DBusPropertiesMap& propertiesList) {
        onPcieSlotGetAllDone(asyncResp, ec1, propertiesList, pcieSlotPath);
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

    asyncResp->res.jsonValue["@odata.type"] = "#PCIeSlots.v1_4_1.PCIeSlots";
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
            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisID, pcieSlotPath, connectionName](
                    const boost::system::error_code& ec1,
                    const std::variant<std::vector<std::string>>& endpoints) {
                onMapperAssociationDone(asyncResp, chassisID, pcieSlotPath,
                                        connectionName, ec1, endpoints);
                },
                "xyz.openbmc_project.ObjectMapper",
                std::string{pcieSlotAssociationPath},
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
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

    crow::connections::systemBus->async_method_call(
        [asyncResp,
         chassisID](const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        onMapperSubtreeDone(asyncResp, chassisID, ec, subtree);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.PCIeSlot"});
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
