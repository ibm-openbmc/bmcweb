#pragma once

#include "dbus_utility.hpp"
#include "logging.hpp"

#include <boost/system/error_code.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{
constexpr std::array<std::string_view, 1> fanInterface = {
    "xyz.openbmc_project.Inventory.Item.Fan"};

namespace fan_utils
{
constexpr std::array<std::string_view, 1> sensorInterface = {
    "xyz.openbmc_project.Sensor.Value"};

inline void afterGetFanSensorObjects(
    const std::function<
        void(const boost::system::error_code& ec,
             std::vector<std::pair<std::string, std::string>>&)>& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    std::vector<std::pair<std::string, std::string>> sensorsPathAndService;

    if (ec)
    {
        // Callback handles error
        BMCWEB_LOG_DEBUG("DBUS response error for getAssociatedSubTree");
        callback(ec, sensorsPathAndService);
        return;
    }
    for (const auto& [sensorPath, serviceMaps] : subtree)
    {
        for (const auto& [service, interfaces] : serviceMaps)
        {
            sensorsPathAndService.emplace_back(service, sensorPath);
        }
    }

    callback(ec, sensorsPathAndService);
}

inline void getFanSensorObjects(
    const std::string& fanPath,
    const std::function<
        void(const boost::system::error_code& ec,
             std::vector<std::pair<std::string, std::string>>&)>& callback)
{
    sdbusplus::message::object_path endpointPath{fanPath};
    endpointPath /= "sensors";
    dbus::utility::getAssociatedSubTree(
        endpointPath,
        sdbusplus::message::object_path("/xyz/openbmc_project/sensors"), 0,
        sensorInterface, std::bind_front(afterGetFanSensorObjects, callback));
}

inline void getFanPaths(
    const std::string& validChassisPath,
    std::function<void(const boost::system::error_code& ec,
                       const dbus::utility::MapperGetSubTreePathsResponse&
                           fanPaths)>&& callback)
{
    sdbusplus::message::object_path endpointPath{validChassisPath};
    endpointPath /= "cooled_by";

    dbus::utility::getAssociatedSubTreePaths(
        endpointPath,
        sdbusplus::message::object_path("/xyz/openbmc_project/inventory"), 0,
        fanInterface, std::move(callback));
}

} // namespace fan_utils
} // namespace redfish
