#pragma once

#include "app.hpp"
#include "dbus_utility.hpp"
#include "led.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/power_supply_utils.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace redfish
{
static constexpr const char* inventoryPath = "/xyz/openbmc_project/inventory";
static constexpr std::array<std::string_view, 1> powerSupplyInterface = {
    "xyz.openbmc_project.Inventory.Item.PowerSupply"};

inline void
    updatePowerSupplyList(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          const std::string& powerSupplyPath)
{
    std::string powerSupplyName =
        sdbusplus::message::object_path(powerSupplyPath).filename();
    if (powerSupplyName.empty())
    {
        return;
    }

    nlohmann::json item = nlohmann::json::object();
    item["@odata.id"] = crow::utility::urlFromPieces(
        "redfish", "v1", "Chassis", chassisId, "PowerSubsystem",
        "PowerSupplies", powerSupplyName);

    nlohmann::json& powerSupplyList = asyncResp->res.jsonValue["Members"];
    powerSupplyList.emplace_back(std::move(item));
    asyncResp->res.jsonValue["Members@odata.count"] = powerSupplyList.size();
}

inline void
    doPowerSupplyCollection(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId,
                            const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/PowerSupplyCollection/PowerSupplyCollection.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] =
        "#PowerSupplyCollection.PowerSupplyCollection";
    asyncResp->res.jsonValue["Name"] = "Power Supply Collection";
    asyncResp->res.jsonValue["@odata.id"] =
        crow::utility::urlFromPieces("redfish", "v1", "Chassis", chassisId,
                                     "PowerSubsystem", "PowerSupplies");
    asyncResp->res.jsonValue["Description"] =
        "The collection of PowerSupply resource instances.";
    asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
    asyncResp->res.jsonValue["Members@odata.count"] = 0;

    std::string powerPath = *validChassisPath + "/powered_by";
    dbus::utility::getAssociatedSubTreePaths(
        powerPath, sdbusplus::message::object_path(inventoryPath), 0,
        powerSupplyInterface,
        [asyncResp,
         chassisId](const boost::system::error_code& ec,
                    const dbus::utility::MapperEndPoints& endpoints) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        for (const auto& endpoint : endpoints)
        {
            updatePowerSupplyList(asyncResp, chassisId, endpoint);
        }
    });
}

inline void handlePowerSupplyCollectionHead(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp,
         chassisId](const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/PowerSupplyCollection/PowerSupplyCollection.json>; rel=describedby");
    });
}

inline void handlePowerSupplyCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doPowerSupplyCollection, asyncResp, chassisId));
}

inline void requestRoutesPowerSupplyCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PowerSubsystem/PowerSupplies/")
        .privileges(redfish::privileges::headPowerSupplyCollection)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handlePowerSupplyCollectionHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PowerSubsystem/PowerSupplies/")
        .privileges(redfish::privileges::getPowerSupplyCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePowerSupplyCollectionGet, std::ref(app)));
}

inline void
    getPowerSupplyState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& service, const std::string& path,
                        bool available)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Inventory.Item", "Present",
        [asyncResp, available](const boost::system::error_code ec,
                               const bool value) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        if (!value)
        {
            asyncResp->res.jsonValue["Status"]["State"] = "Absent";
        }
        else if (!available)
        {
            asyncResp->res.jsonValue["Status"]["State"] = "UnavailableOffline";
        }
    });
}

inline void
    getPowerSupplyHealth(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& service, const std::string& path,
                         bool available)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.State.Decorator.OperationalStatus", "Functional",
        [asyncResp, available](const boost::system::error_code ec,
                               const bool value) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        if (!value || !available)
        {
            asyncResp->res.jsonValue["Status"]["Health"] = "Critical";
        }
    });
}

inline void getPowerSupplyStateAndHealth(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.State.Decorator.Availability", "Available",
        [asyncResp, service, path](const boost::system::error_code ec,
                                   const bool available) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        getPowerSupplyState(asyncResp, service, path, available);

        getPowerSupplyHealth(asyncResp, service, path, available);
    });
}

inline void
    getPowerSupplyAsset(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& service, const std::string& path)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        const std::string* partNumber = nullptr;
        const std::string* serialNumber = nullptr;
        const std::string* manufacturer = nullptr;
        const std::string* model = nullptr;
        const std::string* sparePartNumber = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesList, "PartNumber",
            partNumber, "SerialNumber", serialNumber, "Manufacturer",
            manufacturer, "Model", model, "SparePartNumber", sparePartNumber);

        if (!success)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        if (partNumber != nullptr)
        {
            asyncResp->res.jsonValue["PartNumber"] = *partNumber;
        }

        if (serialNumber != nullptr)
        {
            asyncResp->res.jsonValue["SerialNumber"] = *serialNumber;
        }

        if (manufacturer != nullptr)
        {
            asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;
        }

        if (model != nullptr)
        {
            asyncResp->res.jsonValue["Model"] = *model;
        }

        // SparePartNumber is optional on D-Bus so skip if it is empty
        if (sparePartNumber != nullptr && !sparePartNumber->empty())
        {
            asyncResp->res.jsonValue["SparePartNumber"] = *sparePartNumber;
        }
    });
}

inline void getPowerSupplyFirmwareVersion(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Software.Version", "Version",
        [asyncResp](const boost::system::error_code ec,
                    const std::string& value) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }
        asyncResp->res.jsonValue["FirmwareVersion"] = value;
    });
}

inline void
    getPowerSupplyLocation(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& service, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp](const boost::system::error_code ec,
                    const std::string& value) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }
        asyncResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
            value;
    });
}

inline void
    getEfficiencyPercent(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    static const std::string efficiencyIntf =
        "xyz.openbmc_project.Control.PowerSupplyAttributes";
    // Gets the Power Supply Attributes such as EfficiencyPercent.
    // Currently we only support one power supply EfficiencyPercent, use this
    // for all the power supplies.
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            if (ec.value() == EBADR)
            {
                return;
            }
            BMCWEB_LOG_ERROR << "respHandler DBus error " << ec.message();
            messages::internalError(asyncResp->res);
            return;
        }

        for (const auto& [path, serviceMap] : subtree)
        {
            for (const auto& [service, interfaces] : serviceMap)
            {
                sdbusplus::asio::getProperty<uint32_t>(
                    *crow::connections::systemBus, service, path,
                    efficiencyIntf, "DeratingFactor",
                    [asyncResp](const boost::system::error_code ec1,
                                const uint32_t value) {
                    if (ec1 || value == 0)
                    {
                        return;
                    }

                    nlohmann::json item;
                    item["EfficiencyPercent"] = value;
                    nlohmann::json& efficiencyList =
                        asyncResp->res.jsonValue["EfficiencyRatings"];
                    efficiencyList.emplace_back(std::move(item));
                });
            }
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project", 0,
        std::array<const char*, 1>{efficiencyIntf.c_str()});
}

inline void
    getPowerSupplyMetrics(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          const std::string& powerSupplyId,
                          const std::string& validPowerSupplyPath)
{
    redfish::power_supply_utils::getInputHistoryPaths(
        asyncResp, validPowerSupplyPath,
        [asyncResp, chassisId,
         powerSupplyId](const std::vector<std::string>& historyPaths) {
        if (!historyPaths.empty())
        {
            asyncResp->res.jsonValue["Metrics"]["@odata.id"] =
                crow::utility::urlFromPieces(
                    "redfish", "v1", "Chassis", chassisId, "PowerSubsystem",
                    "PowerSupplies", powerSupplyId, "Metrics");
        }
    });
}

inline void
    doPowerSupplyGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId,
                     const std::string& powerSupplyId,
                     const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    // Get the correct Path and Service that match the input parameters
    redfish::power_supply_utils::getValidPowerSupplyPath(
        asyncResp, *validChassisPath, powerSupplyId,
        [asyncResp, chassisId,
         powerSupplyId](const std::string& powerSupplyPath) {
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/PowerSupply/PowerSupply.json>; rel=describedby");
        asyncResp->res.jsonValue["@odata.type"] =
            "#PowerSupply.v1_5_0.PowerSupply";
        asyncResp->res.jsonValue["Name"] = powerSupplyId;
        asyncResp->res.jsonValue["Id"] = powerSupplyId;
        asyncResp->res.jsonValue["@odata.id"] = crow::utility::urlFromPieces(
            "redfish", "v1", "Chassis", chassisId, "PowerSubsystem",
            "PowerSupplies", powerSupplyId);

        asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
        asyncResp->res.jsonValue["Status"]["Health"] = "OK";

        dbus::utility::getDbusObject(
            powerSupplyPath, powerSupplyInterface,
            [asyncResp,
             powerSupplyPath](const boost::system::error_code& ec,
                              const dbus::utility::MapperGetObject& object) {
            if (ec || object.empty())
            {
                messages::internalError(asyncResp->res);
                return;
            }

            getPowerSupplyStateAndHealth(asyncResp, object.begin()->first,
                                         powerSupplyPath);
            getPowerSupplyAsset(asyncResp, object.begin()->first,
                                powerSupplyPath);
            getPowerSupplyFirmwareVersion(asyncResp, object.begin()->first,
                                          powerSupplyPath);
            getPowerSupplyLocation(asyncResp, object.begin()->first,
                                   powerSupplyPath);
        });

        getEfficiencyPercent(asyncResp);
        getLocationIndicatorActive(asyncResp, powerSupplyPath);
        getPowerSupplyMetrics(asyncResp, chassisId, powerSupplyId,
                              powerSupplyPath);
    });
}

inline void
    handlePowerSupplyHead(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          const std::string& powerSupplyId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp, chassisId,
         powerSupplyId](const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }

        // Get the correct Path and Service that match the input parameters
        redfish::power_supply_utils::getValidPowerSupplyPath(
            asyncResp, *validChassisPath, powerSupplyId,
            [asyncResp](const std::string&) {
            asyncResp->res.addHeader(
                boost::beast::http::field::link,
                "</redfish/v1/JsonSchemas/PowerSupply/PowerSupply.json>; rel=describedby");
        });
    });
}

inline void
    handlePowerSupplyGet(App& app, const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisId,
                         const std::string& powerSupplyId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doPowerSupplyGet, asyncResp, chassisId, powerSupplyId));
}

inline void
    doPatchPowerSupply(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::optional<bool> locationIndicatorActive,
                       const std::string& powerSupplyPath)
{
    if (locationIndicatorActive)
    {
        setLocationIndicatorActive(asyncResp, powerSupplyPath,
                                   *locationIndicatorActive);
    }
}

inline void
    handlePowerSupplyPatch(App& app, const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& chassisId,
                           const std::string& powerSupplyId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<bool> locationIndicatorActive;
    if (!json_util::readJsonPatch(req, asyncResp->res,
                                  "LocationIndicatorActive",
                                  locationIndicatorActive))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp, chassisId, powerSupplyId, locationIndicatorActive](
            const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }

        // Get the correct power supply Path that match the input parameters
        redfish::power_supply_utils::getValidPowerSupplyPath(
            asyncResp, *validChassisPath, powerSupplyId,
            std::bind_front(doPatchPowerSupply, asyncResp,
                            locationIndicatorActive));
    });
}

inline void requestRoutesPowerSupply(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/PowerSubsystem/PowerSupplies/<str>/")
        .privileges(redfish::privileges::headPowerSupply)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handlePowerSupplyHead, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/PowerSubsystem/PowerSupplies/<str>/")
        .privileges(redfish::privileges::getPowerSupply)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePowerSupplyGet, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/PowerSubsystem/PowerSupplies/<str>/")
        .privileges(redfish::privileges::patchPowerSupply)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handlePowerSupplyPatch, std::ref(app)));
}

} // namespace redfish
