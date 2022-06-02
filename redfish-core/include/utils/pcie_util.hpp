#pragma once

#include <string>

namespace redfish
{
namespace pcie_util
{
/**
 * @brief Workaround to handle duplicate PCI device list
 *
 * retrieve PCI device endpoint information and if path is
 * ~/chassisN/io_moduleN/slotN/adapterN then, replace redfish
 * PCI device as "chassisN_io_moduleN_slotN_adapterN"
 *
 * @param[i]   fullPath  object path of PCIe device
 *
 * @return string: unique PCIe device name
 */
inline std::string buildPCIeUniquePath(const std::string& fullPath)
{
    sdbusplus::message::object_path path(fullPath);
    sdbusplus::message::object_path parentPath = path.parent_path();
    sdbusplus::message::object_path parentofParentPath =
        parentPath.parent_path();
    std::string devName;

    if (!parentofParentPath.parent_path().filename().empty())
    {
        devName = parentofParentPath.parent_path().filename() + "_";
    }
    if (!parentofParentPath.filename().empty())
    {
        devName += parentofParentPath.filename() + "_";
    }
    if (!parentPath.filename().empty())
    {
        devName += parentPath.filename() + "_";
    }
    devName += path.filename();
    return devName;
}

} // namespace pcie_util
} // namespace redfish
