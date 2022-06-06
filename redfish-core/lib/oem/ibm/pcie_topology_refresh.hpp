#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "redfish_util.hpp"

#include <variant>

namespace redfish
{

/**
 * @brief Retrieves PCIe Topology Refresh properties over DBUS
 *
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void
    getPCIeTopologyRefresh(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                std::variant<bool>& pcieRefreshValue) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
                messages::internalError(aResp->res);
                return;
            }
            const bool* pcieRefreshValuePtr =
                std::get_if<bool>(&pcieRefreshValue);
            if (!pcieRefreshValuePtr)
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["Oem"]["@odata.type"] =
                "#OemComputerSystem.Oem";
            nlohmann::json& pcieRefresh = aResp->res.jsonValue["Oem"]["IBM"];
            pcieRefresh["@odata.type"] = "#OemComputerSystem.IBM";
            pcieRefresh["PCIeTopologyRefresh"] = *pcieRefreshValuePtr;
        },
        "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/pldm",
        "org.freedesktop.DBus.Properties", "Get", "com.ibm.PLDM.PCIeTopology",
        "PCIeTopologyRefresh");
}

/**
 * @brief Sets PCIe Topology Refresh state.
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] state   PCIe Topology Refresh state from request.
 *
 * @return None.
 */
inline void
    setPCIeTopologyRefresh(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const bool state)
{
    BMCWEB_LOG_DEBUG << "Set PCIe Topology Refresh status.";
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "PCIe Topology Refresh failed." << ec;
                messages::internalError(aResp->res);
                return;
            }
        },
        "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/pldm",
        "org.freedesktop.DBus.Properties", "Set", "com.ibm.PLDM.PCIeTopology",
        "PCIeTopologyRefresh", std::variant<bool>(state));
}

} // namespace redfish