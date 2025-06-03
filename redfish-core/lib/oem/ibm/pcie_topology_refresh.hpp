#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "redfish_util.hpp"

#include <variant>

namespace redfish
{
// Timer for PCIe Topology Refresh
static std::unique_ptr<boost::asio::steady_timer> pcieTopologyRefreshTimer;
static uint count = 0;

/**
 * @brief PCIe Topology Refresh monitor. which block incoming request
 *
 * @param[in] timer       pointer to steady timer which block call
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] count   how many time this method get called.
 *
 * @return None.
 */
static void pcieTopologyRefreshWatchdog(
    const boost::system::error_code& ec, boost::asio::steady_timer* timer,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, uint* countPtr)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR << "steady_timer error " << ec;
        messages::internalError(aResp->res);
        pcieTopologyRefreshTimer = nullptr;
        (*countPtr) = 0;
        return;
    }
    //  This method can block incoming requests max for 8 seconds. So, each
    //  call to this method adds a 1-second block, and the maximum call allow is
    //  8 times which makes it a total of ~8 seconds
    if ((*countPtr) >= 8)
    {
        messages::internalError(aResp->res);
        pcieTopologyRefreshTimer = nullptr;
        (*countPtr) = 0;
        return;
    }
    ++(*countPtr);
    crow::connections::systemBus->async_method_call(
        [aResp, timer, countPtr](const boost::system::error_code ec1,
                                 std::variant<bool>& pcieRefreshValue) {
        if (ec1)
        {
            BMCWEB_LOG_ERROR << "DBUS response error " << ec1;
            messages::internalError(aResp->res);
            pcieTopologyRefreshTimer = nullptr;
            (*countPtr) = 0;
            return;
        }
        const bool* pcieRefreshValuePtr = std::get_if<bool>(&pcieRefreshValue);
        if (pcieRefreshValuePtr == nullptr)
        {
            BMCWEB_LOG_ERROR << "pcieRefreshValuePtr value nullptr";
            messages::internalError(aResp->res);
            pcieTopologyRefreshTimer = nullptr;
            (*countPtr) = 0;
            return;
        }
        // After PCIe Topology Refresh, it sets the pcieRefreshValuePtr
        // value to false. if a value is not false, extend the time, and if
        // it is false, delete the timer and reset the counter
        if (*pcieRefreshValuePtr)
        {
            BMCWEB_LOG_ERROR << "pcieRefreshValuePtr time extended";
            timer->expires_at(timer->expiry() +
                              boost::asio::chrono::seconds(1));
            timer->async_wait(
                [timer, aResp, countPtr](const boost::system::error_code ec2) {
                pcieTopologyRefreshWatchdog(ec2, timer, aResp, countPtr);
            });
        }
        else
        {
            BMCWEB_LOG_ERROR << "pcieRefreshValuePtr value refreshed";
            pcieTopologyRefreshTimer = nullptr;
            (*countPtr) = 0;
            return;
        }
        },
        "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/pldm",
        "org.freedesktop.DBus.Properties", "Get", "com.ibm.PLDM.PCIeTopology",
        "PCIeTopologyRefresh");
};

/**
 * @brief Sets PCIe Topology Refresh state.
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] state   PCIe Topology Refresh state from request.
 *
 * @return None.
 */
inline void
    setPCIeTopologyRefresh(const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const bool state)
{
    BMCWEB_LOG_DEBUG << "Set PCIe Topology Refresh status.";
    crow::connections::systemBus->async_method_call(
        [req, aResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "PCIe Topology Refresh failed." << ec;
            messages::internalError(aResp->res);
            return;
        }
        count = 0;
        pcieTopologyRefreshTimer =
            std::make_unique<boost::asio::steady_timer>(*req.ioService);
        pcieTopologyRefreshTimer->expires_after(std::chrono::seconds(1));
        pcieTopologyRefreshTimer->async_wait(
            [timer = pcieTopologyRefreshTimer.get(),
             aResp](const boost::system::error_code ec1) {
            pcieTopologyRefreshWatchdog(ec1, timer, aResp, &count);
        });
        },
        "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/pldm",
        "org.freedesktop.DBus.Properties", "Set", "com.ibm.PLDM.PCIeTopology",
        "PCIeTopologyRefresh", std::variant<bool>(state));
}

/**
 * @brief Sets Save PCIe Topology Info state.
 *
 * @param[in] aResp   Shared pointer for generating response message.
 * @param[in] state   Save PCIe Topology Info state from request.
 *
 * @return None.
 */
inline void
    setSavePCIeTopologyInfo(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const bool state)
{
    BMCWEB_LOG_DEBUG << "Set Save PCIe Topology Info status.";
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "Save PCIe Topology Info failed." << ec;
            messages::internalError(aResp->res);
            return;
        }
        },
        "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/pldm",
        "org.freedesktop.DBus.Properties", "Set", "com.ibm.PLDM.PCIeTopology",
        "SavePCIeTopologyInfo", std::variant<bool>(state));
}

} // namespace redfish
