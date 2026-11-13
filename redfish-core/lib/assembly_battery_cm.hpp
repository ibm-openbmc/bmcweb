// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_response.hpp"
#include "logging.hpp"

#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace redfish
{

inline void afterGetReadyToRemoveOfTodBattery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::size_t assemblyIndex, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& /*unused*/)
{
    nlohmann::json& assemblyArray = asyncResp->res.jsonValue["Assemblies"];
    if (ec)
    {
        if (ec.value() == boost::system::errc::io_error)
        {
            // Battery voltage is not on DBUS so ADCSensor is not
            // running.
            nlohmann::json& oemOpenBMC =
                assemblyArray.at(assemblyIndex)["Oem"]["OpenBMC"];
            oemOpenBMC["@odata.type"] = "#OpenBMCAssembly.v1_0_0.OpenBMC";
            oemOpenBMC["ReadyToRemove"] = true;
            return;
        }
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }
    nlohmann::json& oemOpenBMC =
        assemblyArray.at(assemblyIndex)["Oem"]["OpenBMC"];
    oemOpenBMC["@odata.type"] = "#OpenBMCAssembly.v1_0_0.OpenBMC";
    oemOpenBMC["ReadyToRemove"] = false;
}

inline void getReadyToRemoveOfTodBattery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::size_t assemblyIndex)
{
    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/sensors/voltage/Battery_Voltage", {},
        std::bind_front(afterGetReadyToRemoveOfTodBattery, asyncResp,
                        assemblyIndex));
}

inline void startOrStopADCSensor(
    const bool start, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string method{"StartUnit"};
    if (!start)
    {
        method = "StopUnit";
    }

    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to start or stop ADCSensor:{}",
                                 ec.value());
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        },
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", method,
        "xyz.openbmc_project.adcsensor.service", "replace");
}

inline void afterGetDbusObjectDoBatteryCM(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& assembly, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }

    for (const auto& [serviceName, interfaceList] : object)
    {
        auto ifaceIt = std::ranges::find(
            interfaceList,
            "xyz.openbmc_project.State.Decorator.OperationalStatus");

        if (ifaceIt == interfaceList.end())
        {
            continue;
        }

        sdbusplus::asio::setProperty(
            *crow::connections::systemBus, serviceName, assembly,
            "xyz.openbmc_project.State.Decorator."
            "OperationalStatus",
            "Functional", true,
            [asyncResp, assembly](const boost::system::error_code& ec2) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR(
                        "Failed to set functional property on battery: {} ",
                        ec2.value());
                    messages::internalError(asyncResp->res);
                    return;
                }
                startOrStopADCSensor(true, asyncResp);
            });
        return;
    }

    BMCWEB_LOG_ERROR("No OperationalStatus interface on {}", assembly);
    messages::internalError(asyncResp->res);
}

inline void doBatteryCM(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& assembly, const bool readyToRemove)
{
    if (readyToRemove)
    {
        // Stop the adcsensor service so it doesn't monitor the battery
        startOrStopADCSensor(false, asyncResp);
        return;
    }

    // Find the service that has the OperationalStatus iface, set the
    // Functional property back to true, and then start the adcsensor service.
    std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.State.Decorator.OperationalStatus"};
    dbus::utility::getDbusObject(
        assembly, interfaces,
        std::bind_front(afterGetDbusObjectDoBatteryCM, asyncResp, assembly));
}

} // namespace redfish
