#pragma once

#include <string>

namespace redfish
{
namespace fabric_util
{
/**
 * @brief Workaround to handle duplicate Fabric device list
 *
 * retrieve Fabric device endpoint information and if path is
 * ~/chassisN/logical_slotN/io_moduleN then, replace redfish
 * Fabric device as "chassisN-logical_slotN-io_moduleN"
 *
 * @param[i]   fullPath  object path of Fabric device
 *
 * @return string: unique Fabric device name
 */
inline std::string buildFabricUniquePath(const std::string& fullPath)
{
    sdbusplus::message::object_path path(fullPath);
    sdbusplus::message::object_path parentPath = path.parent_path();

    std::string devName;

    if (!parentPath.parent_path().filename().empty())
    {
        devName = parentPath.parent_path().filename() + "-";
    }
    if (!parentPath.filename().empty())
    {
        devName += parentPath.filename() + "-";
    }
    devName += path.filename();
    return devName;
}

} // namespace fabric_util
} // namespace redfish
