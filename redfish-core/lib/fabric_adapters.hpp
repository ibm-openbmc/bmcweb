#pragma once

#include "app.hpp"
#include "dbus_utility.hpp"
#include "led.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/fabric_util.hpp"
#include "utils/json_utils.hpp"
#include "utils/pcie_util.hpp"

#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace redfish
{

inline void handleAdapterError(const boost::system::error_code& ec,
                               crow::Response& res,
                               const std::string& adapterId)
{

    if (ec.value() == boost::system::errc::io_error)
    {
        messages::resourceNotFound(res, "#FabricAdapter.v1_4_0.FabricAdapter",
                                   adapterId);
        return;
    }

    BMCWEB_LOG_ERROR << "DBus method call failed with error " << ec.value();
    messages::internalError(res);
}

inline void
    getFabricAdapterLocation(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& serviceName,
                             const std::string& fabricAdapterPath)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, serviceName, fabricAdapterPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [aResp](const boost::system::error_code ec,
                const std::string& property) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR << "DBUS response error for Location";
                messages::internalError(aResp->res);
            }
            return;
        }

        aResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
            property;
        });
}

inline void
    getFabricAdapterAsset(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const std::string& serviceName,
                          const std::string& fabricAdapterPath)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, serviceName, fabricAdapterPath,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [fabricAdapterPath,
         aResp{aResp}](const boost::system::error_code ec,
                       const dbus::utility::DBusPropertiesMap& propertiesList) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR << "DBUS response error for Properties";
                messages::internalError(aResp->res);
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
            messages::internalError(aResp->res);
            return;
        }

        if (serialNumber != nullptr)
        {
            aResp->res.jsonValue["SerialNumber"] = *serialNumber;
        }

        if (model != nullptr)
        {
            aResp->res.jsonValue["Model"] = *model;
        }

        if (partNumber != nullptr)
        {
            aResp->res.jsonValue["PartNumber"] = *partNumber;
        }

        if (sparePartNumber != nullptr && !sparePartNumber->empty())
        {
            aResp->res.jsonValue["SparePartNumber"] = *sparePartNumber;
        }
        });
}

inline void
    getFabricAdapterState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const std::string& serviceName,
                          const std::string& fabricAdapterPath)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, serviceName, fabricAdapterPath,
        "xyz.openbmc_project.Inventory.Item", "Present",
        [aResp](const boost::system::error_code ec, const bool present) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR << "DBUS response error for State";
                messages::internalError(aResp->res);
            }
            return;
        }

        if (!present)
        {
            aResp->res.jsonValue["Status"]["State"] = "Absent";
        }
        });
}

inline void
    getFabricAdapterHealth(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& serviceName,
                           const std::string& fabricAdapterPath)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, serviceName, fabricAdapterPath,
        "xyz.openbmc_project.State.Decorator.OperationalStatus", "Functional",
        [aResp](const boost::system::error_code ec, const bool functional) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR << "DBUS response error for Health";
                messages::internalError(aResp->res);
            }
            return;
        }

        if (!functional)
        {
            aResp->res.jsonValue["Status"]["Health"] = "Critical";
        }
        });
}

inline void linkAsPCIeDevice(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& fabricAdapterPath)
{
    const std::string pcieDeviceName =
        pcie_util::buildPCIeUniquePath(fabricAdapterPath);

    if (pcieDeviceName.empty())
    {
        BMCWEB_LOG_ERROR << "Failed to find / in pcie device path";
        messages::internalError(aResp->res);
        return;
    }

    nlohmann::json& deviceArray = aResp->res.jsonValue["Links"]["PCIeDevices"];
    deviceArray = nlohmann::json::array();

    deviceArray.push_back(
        {{"@odata.id",
          "/redfish/v1/Systems/system/PCIeDevices/" + pcieDeviceName}});

    aResp->res.jsonValue["Links"]["PCIeDevices@odata.count"] =
        deviceArray.size();
}

inline void doAdapterGet(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& systemName,
                         const std::string& adapterId,
                         const std::string& fabricAdapterPath,
                         const std::string& serviceName,
                         const dbus::utility::InterfaceList& interfaces)
{
    aResp->res.addHeader(boost::beast::http::field::link,
                         "</redfish/v1/JsonSchemas/FabricAdapter/"
                         "FabricAdapter.json>; rel=describedby");
    aResp->res.jsonValue["@odata.type"] = "#FabricAdapter.v1_4_0.FabricAdapter";
    aResp->res.jsonValue["Name"] = "Fabric Adapter";
    aResp->res.jsonValue["Id"] = adapterId;
    aResp->res.jsonValue["@odata.id"] = crow::utility::urlFromPieces(
        "redfish", "v1", "Systems", systemName, "FabricAdapters", adapterId);
    aResp->res.jsonValue["Ports"]["@odata.id"] =
        crow::utility::urlFromPieces("redfish", "v1", "Systems", systemName,
                                     "FabricAdapters", adapterId, "Ports");

    aResp->res.jsonValue["Status"]["State"] = "Enabled";
    aResp->res.jsonValue["Status"]["Health"] = "OK";

    getFabricAdapterLocation(aResp, serviceName, fabricAdapterPath);
    getFabricAdapterAsset(aResp, serviceName, fabricAdapterPath);
    getFabricAdapterState(aResp, serviceName, fabricAdapterPath);
    getFabricAdapterHealth(aResp, serviceName, fabricAdapterPath);
    getLocationIndicatorActive(aResp, fabricAdapterPath);

    // if the adapter also implements this interface, link the adapter schema to
    // PCIeDevice schema for this adapter.
    if (std::find(interfaces.begin(), interfaces.end(),
                  "xyz.openbmc_project.Inventory.Item.PCIeDevice") !=
        interfaces.end())
    {
        linkAsPCIeDevice(aResp, fabricAdapterPath);
    }
}

inline void getValidFabricAdapterPath(
    const std::string& adapterId, const std::string& systemName,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    std::function<void(
        const std::string& fabricAdapterPath, const std::string& serviceName,
        const dbus::utility::InterfaceList& interfaces)>&& callback)
{
    if (systemName != "system")
    {
        messages::resourceNotFound(aResp->res, "ComputerSystem", systemName);
        return;
    }
    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Inventory.Item.FabricAdapter"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [adapterId, aResp,
         callback](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            handleAdapterError(ec, aResp->res, adapterId);
            return;
        }
        for (const auto& [adapterPath, serviceMap] : subtree)
        {
            std::string adapterUniq =
                fabric_util::buildFabricUniquePath(adapterPath);
            if (fabric_util::checkFabricAdapterId(adapterId, adapterUniq))
            {
                nlohmann::json::json_pointer ptr("/Name");
                name_util::getPrettyName(aResp, adapterPath, serviceMap, ptr);
                callback(adapterPath, serviceMap.begin()->first,
                         serviceMap.begin()->second);
                return;
            }
        }
        BMCWEB_LOG_WARNING << "Adapter not found";
        messages::resourceNotFound(aResp->res, "FabricAdapter", adapterId);
        });
}

inline void
    handleFabricAdapterGet(App& app, const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& systemName,
                           const std::string& adapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    getValidFabricAdapterPath(
        adapterId, systemName, aResp,
        [aResp, systemName,
         adapterId](const std::string& fabricAdapterPath,
                    const std::string& serviceName,
                    const dbus::utility::InterfaceList& interfaces) {
        doAdapterGet(aResp, systemName, adapterId, fabricAdapterPath,
                     serviceName, interfaces);
        });
}

inline void
    handleFabricAdapterPatch(App& app, const crow::Request& req,
                             const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& systemName,
                             const std::string& adapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    std::optional<bool> locationIndicatorActive;

    if (!json_util::readJsonPatch(req, aResp->res, "LocationIndicatorActive",
                                  locationIndicatorActive))
    {
        return;
    }

    getValidFabricAdapterPath(
        adapterId, systemName, aResp,
        [req, aResp, locationIndicatorActive](
            const std::string& fabricAdapterPath,
            [[maybe_unused]] const std::string& serviceName,
            [[maybe_unused]] const dbus::utility::InterfaceList& interfaces) {
        if (locationIndicatorActive)
        {
            setLocationIndicatorActive(aResp, fabricAdapterPath,
                                       *locationIndicatorActive);
        }
        });
}

inline void handleFabricAdapterCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != "system")
    {
        messages::resourceNotFound(aResp->res, "ComputerSystem", systemName);
        return;
    }

    aResp->res.addHeader(boost::beast::http::field::link,
                         "</redfish/v1/JsonSchemas/FabricAdapterCollection/"
                         "FabricAdapterCollection.json>; rel=describedby");
    aResp->res.jsonValue["@odata.type"] =
        "#FabricAdapterCollection.FabricAdapterCollection";
    aResp->res.jsonValue["Name"] = "Fabric Adapter Collection";
    aResp->res.jsonValue["@odata.id"] = crow::utility::urlFromPieces(
        "redfish", "v1", "Systems", systemName, "FabricAdapters");

    auto getMembersFromPaths =
        [](std::vector<std::string>& pathNames,
           const dbus::utility::MapperGetSubTreePathsResponse& objects) {
        for (const auto& object : objects)
        {
            std::string leaf = fabric_util::buildFabricUniquePath(object);
            if (leaf.empty())
            {
                continue;
            }
            pathNames.push_back(leaf);
        }
    };

    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Inventory.Item.FabricAdapter"};
    collection_util::getCollectionMembersWithPathConversion(
        aResp, boost::urls::url("/redfish/v1/Systems/system/FabricAdapters"),
        interfaces, std::move(getMembersFromPaths));
}

inline void handleFabricAdapterCollectionHead(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != "system")
    {
        messages::resourceNotFound(aResp->res, "ComputerSystem", systemName);
        return;
    }
    aResp->res.addHeader(boost::beast::http::field::link,
                         "</redfish/v1/JsonSchemas/FabricAdapterCollection/"
                         "FabricAdapterCollection.json>; rel=describedby");
}

inline void
    handleFabricAdapterHead(crow::App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& systemName,
                            const std::string& adapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    getValidFabricAdapterPath(
        adapterId, systemName, aResp,
        [aResp, systemName, adapterId](const std::string&, const std::string&,
                                       const dbus::utility::InterfaceList&) {
        aResp->res.addHeader(boost::beast::http::field::link,
                             "</redfish/v1/JsonSchemas/FabricAdapter/"
                             "FabricAdapter.json>; rel=describedby");
        });
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

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/FabricAdapters/<str>/")
        .privileges(redfish::privileges::patchFabricAdapter)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleFabricAdapterPatch, std::ref(app)));
}
} // namespace redfish
