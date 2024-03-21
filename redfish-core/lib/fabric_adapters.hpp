#pragma once

#include "app.hpp"
#include "dbus_utility.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/fabric_util.hpp"
#include "utils/json_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace redfish
{

inline void getFabricAdapterLocation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& serviceName, const std::string& fabricAdapterPath)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, serviceName, fabricAdapterPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& property) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR("DBUS response error for Location");
                messages::internalError(asyncResp->res);
            }
            return;
        }

        asyncResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
            property;
    });
}

inline void
    getFabricAdapterAsset(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& serviceName,
                          const std::string& fabricAdapterPath)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, serviceName, fabricAdapterPath,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [fabricAdapterPath, asyncResp{asyncResp}](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& propertiesList) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR("DBUS response error for Properties");
                messages::internalError(asyncResp->res);
            }
            return;
        }

        const std::string* serialNumber = nullptr;
        const std::string* model = nullptr;
        const std::string* partNumber = nullptr;
        const std::string* sparePartNumber = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesList, "SerialNumber",
            serialNumber, "Model", model, "PartNumber", partNumber,
            "SparePartNumber", sparePartNumber);

        if (!success)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        if (serialNumber != nullptr)
        {
            asyncResp->res.jsonValue["SerialNumber"] = *serialNumber;
        }

        if (model != nullptr)
        {
            asyncResp->res.jsonValue["Model"] = *model;
        }

        if (partNumber != nullptr)
        {
            asyncResp->res.jsonValue["PartNumber"] = *partNumber;
        }

        if (sparePartNumber != nullptr && !sparePartNumber->empty())
        {
            asyncResp->res.jsonValue["SparePartNumber"] = *sparePartNumber;
        }
    });
}

inline void
    getFabricAdapterState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& serviceName,
                          const std::string& fabricAdapterPath)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, serviceName, fabricAdapterPath,
        "xyz.openbmc_project.Inventory.Item", "Present",
        [asyncResp](const boost::system::error_code& ec, const bool present) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR("DBUS response error for State");
                messages::internalError(asyncResp->res);
            }
            return;
        }

        if (!present)
        {
            asyncResp->res.jsonValue["Status"]["State"] = "Absent";
        }
    });
}

inline void
    getFabricAdapterHealth(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& serviceName,
                           const std::string& fabricAdapterPath)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, serviceName, fabricAdapterPath,
        "xyz.openbmc_project.State.Decorator.OperationalStatus", "Functional",
        [asyncResp](const boost::system::error_code& ec,
                    const bool functional) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR("DBUS response error for Health");
                messages::internalError(asyncResp->res);
            }
            return;
        }

        if (!functional)
        {
            asyncResp->res.jsonValue["Status"]["Health"] = "Critical";
        }
    });
}

inline void afterDoCheckFabricAdapterChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const dbus::utility::MapperEndPoints& pcieSlotPaths,
    const std::function<void(const std::string&,
                             const dbus::utility::MapperEndPoints&)>& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& chassisPaths)
{
    if (ec)
    {
        if (ec.value() == EBADR)
        {
            // This PCIeSlot has no chassis association.
            return;
        }
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }
    if (chassisPaths.size() != 1)
    {
        BMCWEB_LOG_ERROR("PCIe Slot association error! ");
        messages::internalError(asyncResp->res);
        return;
    }

    sdbusplus::message::object_path path(chassisPaths[0]);
    std::string chassisName = path.filename();

    callback(chassisName, pcieSlotPaths);
}

inline void doCheckFabricAdapterChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricAdapterPath,
    const dbus::utility::MapperEndPoints& pcieSlotPaths,
    std::function<void(const std::string&,
                       const dbus::utility::MapperEndPoints&)>
        callback)
{
    constexpr std::array<std::string_view, 1> chassisInterface{
        "xyz.openbmc_project.Inventory.Item.Chassis"};
    dbus::utility::getAssociatedSubTreePaths(
        fabricAdapterPath + "/chassis",
        sdbusplus::message::object_path("/xyz/openbmc_project/inventory"), 0,
        chassisInterface,
        std::bind_front(afterDoCheckFabricAdapterChassis, asyncResp,
                        pcieSlotPaths, std::move(callback)));
}

inline void afterGetFabricAdapterPCIeSlots(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricAdapterPath,
    std::function<void(const std::string&,
                       const dbus::utility::MapperEndPoints&)>
        callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& pcieSlotPaths)
{
    if (ec)
    {
        if (ec.value() == EBADR)
        {
            BMCWEB_LOG_DEBUG("Slot association not found");
            return;
        }
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }
    if (pcieSlotPaths.empty())
    {
        // no slot associations
        BMCWEB_LOG_DEBUG("Slot association not found");
        return;
    }

    // Check whether PCIeSlot is associated with chassis
    doCheckFabricAdapterChassis(asyncResp, fabricAdapterPath, pcieSlotPaths,
                                std::move(callback));
}

inline void getFabricAdapterPCIeSlots(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricAdapterPath,
    std::function<void(const std::string&,
                       const dbus::utility::MapperEndPoints&)>&& callback)
{
    constexpr std::array<std::string_view, 1> pcieSlotInterface{
        "xyz.openbmc_project.Inventory.Item.PCIeSlot"};
    dbus::utility::getAssociatedSubTreePaths(
        fabricAdapterPath + "/containing",
        sdbusplus::message::object_path("/xyz/openbmc_project/inventory"), 0,
        pcieSlotInterface,
        std::bind_front(afterGetFabricAdapterPCIeSlots, asyncResp,
                        fabricAdapterPath, std::move(callback)));
}

inline void doAdapterGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& systemName,
                         const std::string& adapterId,
                         const std::string& fabricAdapterPath,
                         const std::string& serviceName)
{
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/FabricAdapter/FabricAdapter.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] =
        "#FabricAdapter.v1_4_0.FabricAdapter";
    asyncResp->res.jsonValue["Name"] = "Fabric Adapter";
    asyncResp->res.jsonValue["Id"] = adapterId;
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/FabricAdapters/{}", systemName, adapterId);

    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
    asyncResp->res.jsonValue["Status"]["Health"] = "OK";

    // add pcieslots
    getFabricAdapterPCIeSlots(
        asyncResp, fabricAdapterPath,
        [asyncResp](const std::string& chassisName,
                    const dbus::utility::MapperEndPoints& /*pcieSlotPaths*/) {
        asyncResp->res.jsonValue["Oem"]["IBM"]["@odata.type"] =
            "#OemFabricAdapter.v1_0_0.IBM";
        asyncResp->res.jsonValue["Oem"]["IBM"]["Slots"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/PCIeSlots",
                                chassisName);
    });

    getFabricAdapterLocation(asyncResp, serviceName, fabricAdapterPath);
    getFabricAdapterAsset(asyncResp, serviceName, fabricAdapterPath);
    getFabricAdapterState(asyncResp, serviceName, fabricAdapterPath);
    getFabricAdapterHealth(asyncResp, serviceName, fabricAdapterPath);
}

inline void afterGetValidFabricAdapterPath(
    const std::string& adapterId,
    std::function<void(const boost::system::error_code&,
                       const std::string& fabricAdapterPath,
                       const std::string& serviceName)>& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    std::string fabricAdapterPath;
    std::string serviceName;
    if (ec)
    {
        callback(ec, fabricAdapterPath, serviceName);
        return;
    }

    for (const auto& [adapterPath, serviceMap] : subtree)
    {
        if (adapterId == fabric_util::buildFabricUniquePath(adapterPath))
        {
            fabricAdapterPath = adapterPath;
            serviceName = serviceMap.begin()->first;
            break;
        }
    }
    callback(ec, fabricAdapterPath, serviceName);
}

inline void getValidFabricAdapterPath(
    const std::string& adapterId,
    std::function<void(const boost::system::error_code& ec,
                       const std::string& fabricAdapterPath,
                       const std::string& serviceName)>&& callback)
{
    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Inventory.Item.FabricAdapter"};
    dbus::utility::getSubTree("/xyz/openbmc_project/inventory", 0, interfaces,
                              std::bind_front(afterGetValidFabricAdapterPath,
                                              adapterId, std::move(callback)));
}

inline void afterHandleFabricAdapterGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& adapterId,
    const boost::system::error_code& ec, const std::string& fabricAdapterPath,
    const std::string& serviceName)
{
    if (ec)
    {
        if (ec.value() == boost::system::errc::io_error)
        {
            messages::resourceNotFound(asyncResp->res, "FabricAdapter",
                                       adapterId);
            return;
        }

        BMCWEB_LOG_ERROR("DBus method call failed with error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }
    if (fabricAdapterPath.empty() || serviceName.empty())
    {
        BMCWEB_LOG_WARNING("Adapter not found");
        messages::resourceNotFound(asyncResp->res, "FabricAdapter", adapterId);
        return;
    }
    doAdapterGet(asyncResp, systemName, adapterId, fabricAdapterPath,
                 serviceName);
}

inline void
    handleFabricAdapterGet(App& app, const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& systemName,
                           const std::string& adapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (bmcwebEnableMultiHost)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != "system")
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    getValidFabricAdapterPath(
        adapterId, std::bind_front(afterHandleFabricAdapterGet, asyncResp,
                                   systemName, adapterId));
}

inline void handleFabricAdapterCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (bmcwebEnableMultiHost)
    {
        // Option currently returns no systems. TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
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
        "</redfish/v1/JsonSchemas/FabricAdapterCollection/FabricAdapterCollection.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] =
        "#FabricAdapterCollection.FabricAdapterCollection";
    asyncResp->res.jsonValue["Name"] = "Fabric Adapter Collection";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/FabricAdapters", systemName);

    fabric_util::getFabricAdapterList(asyncResp,
                                      nlohmann::json::json_pointer("/Members"));
}

inline void handleFabricAdapterCollectionHead(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (bmcwebEnableMultiHost)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
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
        "</redfish/v1/JsonSchemas/FabricAdapterCollection/FabricAdapterCollection.json>; rel=describedby");
}

inline void afterHandleFabricAdapterHead(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& adapterId, const boost::system::error_code& ec,
    const std::string& fabricAdapterPath, const std::string& serviceName)
{
    if (ec)
    {
        if (ec.value() == boost::system::errc::io_error)
        {
            messages::resourceNotFound(asyncResp->res, "FabricAdapter",
                                       adapterId);
            return;
        }

        BMCWEB_LOG_ERROR("DBus method call failed with error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }
    if (fabricAdapterPath.empty() || serviceName.empty())
    {
        BMCWEB_LOG_WARNING("Adapter not found");
        messages::resourceNotFound(asyncResp->res, "FabricAdapter", adapterId);
        return;
    }
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/FabricAdapter/FabricAdapter.json>; rel=describedby");
}

inline void
    handleFabricAdapterHead(crow::App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& systemName,
                            const std::string& adapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if constexpr (bmcwebEnableMultiHost)
    {
        // Option currently returns no systems. TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != "system")
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    getValidFabricAdapterPath(
        adapterId,
        std::bind_front(afterHandleFabricAdapterHead, asyncResp, adapterId));
}

inline void requestRoutesFabricAdapterCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/FabricAdapters/")
        .privileges(redfish::privileges::getFabricAdapterCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleFabricAdapterCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/FabricAdapters/")
        .privileges(redfish::privileges::headFabricAdapterCollection)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleFabricAdapterCollectionHead, std::ref(app)));
}

inline void requestRoutesFabricAdapters(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/FabricAdapters/<str>/")
        .privileges(redfish::privileges::getFabricAdapter)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleFabricAdapterGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/FabricAdapters/<str>/")
        .privileges(redfish::privileges::headFabricAdapter)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleFabricAdapterHead, std::ref(app)));
}
} // namespace redfish
