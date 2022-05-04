#pragma once

#include <app.hpp>
#include <dbus_utility.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/json_utils.hpp>

#include <cstdint>

namespace redfish
{

// PowerCap interface
constexpr auto powerCapInterface = "xyz.openbmc_project.Control.Power.Cap";

inline void getPowerSubsystemAllocationProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objectPath)
{
    // Get all properties of PowerCap D-Bus interface
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "D-Bus response error on GetAll " << ec;
                messages::internalError(asyncResp->res);
                return;
            }

            // Get value of PowerCap properties from D-Bus response
            uint32_t powerCap{0};
            bool powerCapEnable{false};
            uint32_t maxPowerCapValue{0};
            for (const auto& [property, value] : properties)
            {
                if (property == "PowerCap")
                {
                    const uint32_t* valPtr = std::get_if<uint32_t>(&value);
                    if (valPtr == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Unexpected data type for PowerCap";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    powerCap = *valPtr;
                }
                else if (property == "PowerCapEnable")
                {
                    const bool* valPtr = std::get_if<bool>(&value);
                    if (valPtr == nullptr)
                    {
                        BMCWEB_LOG_DEBUG
                            << "Unexpected data type for PowerCapEnable";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    powerCapEnable = *valPtr;
                }
                else if (property == "MaxPowerCapValue")
                {
                    const uint32_t* valPtr = std::get_if<uint32_t>(&value);
                    if (valPtr == nullptr)
                    {
                        BMCWEB_LOG_DEBUG
                            << "Unexpected data type for MaxPowerCapValue";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    maxPowerCapValue = *valPtr;
                }
            }

            // If MaxPowerCapValue valid, store Allocation properties in JSON
            if ((maxPowerCapValue > 0) && (maxPowerCapValue < UINT32_MAX))
            {
                if (powerCapEnable)
                {
                    asyncResp->res.jsonValue["Allocation"]["AllocatedWatts"] =
                        powerCap;
                }
                else
                {
                    asyncResp->res.jsonValue["Allocation"]["AllocatedWatts"] =
                        maxPowerCapValue;
                }
                asyncResp->res.jsonValue["Allocation"]["RequestedWatts"] =
                    maxPowerCapValue;
            }
        },
        service, objectPath, "org.freedesktop.DBus.Properties", "GetAll",
        powerCapInterface);
}

inline void getPowerSubsystemAllocation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Find service and object path that implement PowerCap interface (if any)
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subTree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "D-Bus response error on GetSubTree " << ec;
                messages::internalError(asyncResp->res);
                return;
            }

            if (!subTree.empty())
            {
                const auto& [objectPath, serviceMap] = subTree[0];
                if (!serviceMap.empty())
                {
                    const auto& service = serviceMap[0].first;

                    // Get properties from PowerCap interface and store in JSON
                    getPowerSubsystemAllocationProperties(asyncResp, service,
                                                          objectPath);
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", 0,
        std::array<const char*, 1>{powerCapInterface});
}

inline void
    getPowerSubsystem(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& chassisID)
{
    BMCWEB_LOG_DEBUG
        << "Get properties for PowerSubsystem associated to chassis = "
        << chassisID;

    asyncResp->res.jsonValue = {
        {"@odata.type", "#PowerSubsystem.v1_0_0.PowerSubsystem"},
        {"Name", "Power Subsystem for Chassis"}};
    asyncResp->res.jsonValue["Id"] = "PowerSubsystem";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisID + "/PowerSubsystem";
    asyncResp->res.jsonValue["PowerSupplies"]["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisID + "/PowerSubsystem/PowerSupplies";

    // Get Allocation information from D-Bus and store in JSON
    getPowerSubsystemAllocation(asyncResp);
}

inline void requestRoutesPowerSubsystem(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PowerSubsystem/")
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

                        getPowerSubsystem(asyncResp, chassisID);
                    };
                redfish::chassis_utils::getValidChassisID(
                    asyncResp, chassisID, std::move(getChassisID));
            });
}

} // namespace redfish
