// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "ethernet.hpp"
#include "generated/enums/action_info.hpp"
#include "generated/enums/computer_system.hpp"
#include "generated/enums/resource.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/ip_utils.hpp"
#include "utils/json_utils.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace redfish
{

/**
 * @brief Retrieves hypervisor state properties over dbus
 *
 * The hypervisor state object is optional so this function will only set the
 * state variables if the object is found
 *
 * @param[in] asyncResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void getHypervisorState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Get hypervisor state information.");
    dbus::utility::getProperty<std::string>(
        "xyz.openbmc_project.State.Hypervisor",
        "/xyz/openbmc_project/state/hypervisor0",
        "xyz.openbmc_project.State.Host", "CurrentHostState",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& hostState) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                // This is an optional D-Bus object so just return if
                // error occurs
                return;
            }

            BMCWEB_LOG_DEBUG("Hypervisor state: {}", hostState);
            // Verify Host State
            if (hostState == "xyz.openbmc_project.State.Host.HostState.Running")
            {
                asyncResp->res.jsonValue["PowerState"] =
                    resource::PowerState::On;
                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::Enabled;
            }
            else if (hostState == "xyz.openbmc_project.State.Host.HostState."
                                  "Quiesced")
            {
                asyncResp->res.jsonValue["PowerState"] =
                    resource::PowerState::On;
                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::Quiesced;
            }
            else if (hostState == "xyz.openbmc_project.State.Host.HostState."
                                  "Standby")
            {
                asyncResp->res.jsonValue["PowerState"] =
                    resource::PowerState::On;
                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::StandbyOffline;
            }
            else if (hostState == "xyz.openbmc_project.State.Host.HostState."
                                  "TransitioningToRunning")
            {
                asyncResp->res.jsonValue["PowerState"] =
                    resource::PowerState::PoweringOn;
                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::Starting;
            }
            else if (hostState == "xyz.openbmc_project.State.Host.HostState."
                                  "TransitioningToOff")
            {
                asyncResp->res.jsonValue["PowerState"] =
                    resource::PowerState::PoweringOff;
                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::Enabled;
            }
            else if (hostState ==
                     "xyz.openbmc_project.State.Host.HostState.Off")
            {
                asyncResp->res.jsonValue["PowerState"] =
                    resource::PowerState::Off;
                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::Disabled;
            }
            else
            {
                messages::internalError(asyncResp->res);
                return;
            }
        });
}

/**
 * @brief Populate Actions if any are valid for hypervisor object
 *
 * The hypervisor state object is optional so this function will only set the
 * Action if the object is found
 *
 * @param[in] asyncResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void getHypervisorActions(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Get hypervisor actions.");
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.State.Host"};
    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/state/hypervisor0", interfaces,
        [asyncResp](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                // This is an optional D-Bus object so just return if
                // error occurs
                return;
            }

            if (objInfo.empty())
            {
                // As noted above, this is an optional interface so just return
                // if there is no instance found
                return;
            }

            if (objInfo.size() > 1)
            {
                // More then one hypervisor object is not supported and is an
                // error
                messages::internalError(asyncResp->res);
                return;
            }

            // Object present so system support limited ComputerSystem Action
            nlohmann::json& reset =
                asyncResp->res.jsonValue["Actions"]["#ComputerSystem.Reset"];
            reset["target"] =
                "/redfish/v1/Systems/hypervisor/Actions/ComputerSystem.Reset";
            reset["@Redfish.ActionInfo"] =
                "/redfish/v1/Systems/hypervisor/ResetActionInfo";
        });
}

inline bool translateSlaacEnabledToBool(const std::string& inputDHCP)
{
    return (
        (inputDHCP ==
         "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6stateless") ||
        (inputDHCP ==
         "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4v6stateless"));
}

inline bool translateDHCPEnabledToIPv6AutoConfig(const std::string& inputDHCP)
{
    return (inputDHCP ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4v6stateless") ||
           (inputDHCP ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6stateless");
}

inline std::string translateIPv6AutoConfigToDHCPEnabled(
    const std::string& inputDHCP, const bool& ipv6AutoConfig)
{
    if (ipv6AutoConfig)
    {
        if ((inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4") ||
            (inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.both"))
        {
            return "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4v6stateless";
        }
        if ((inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6") ||
            (inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.none"))
        {
            return "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6stateless";
        }
    }
    if (!ipv6AutoConfig)
    {
        if ((inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4v6stateless") ||
            (inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.both"))
        {
            return "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4";
        }
        if (inputDHCP ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6stateless")
        {
            return "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.none";
        }
    }
    return inputDHCP;
}

inline bool extractIpInterfaceData(
    const dbus::utility::DBusPropertiesMap& properties,
    const std::string& protocol, IPv4AddressData& ipv4Address,
    IPv6AddressData& ipv6Address)
{
    const std::string* address = nullptr;
    const uint8_t* mask = nullptr;
    const std::string* gateway = nullptr;
    const std::string* origin = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "Address", address,
        "PrefixLength", mask, "Gateway", gateway, "Origin", origin);

    if (!success)
    {
        BMCWEB_LOG_INFO(
            "Failed to fetch dbus properties of Hypervisor IP Interface");
        return false;
    }

    if (address != nullptr)
    {
        if (protocol == "ipv4")
        {
            ipv4Address.address = *address;
        }
        else
        {
            ipv6Address.address = *address;
        }
    }

    if (mask != nullptr)
    {
        if (protocol == "ipv4")
        {
            ipv4Address.netmask = getNetmask(*mask);
        }
        else
        {
            ipv6Address.prefixLength = *mask;
        }
    }

    if (gateway != nullptr && protocol == "ipv4")
    {
        ipv4Address.gateway = *gateway;
    }

    if (origin != nullptr)
    {
        if (protocol == "ipv4")
        {
            ipv4Address.origin =
                translateAddressOriginDbusToRedfish(*origin, true);
        }
        else
        {
            ipv6Address.origin =
                translateAddressOriginDbusToRedfish(*origin, false);
        }
    }

    return true;
}

inline void handleEnableProperty(
    const dbus::utility::DBusPropertiesMap& properties,
    const std::string& protocol, IPv4AddressData& ipv4Address,
    IPv6AddressData& ipv6Address)
{
    for (const auto& property : properties)
    {
        if (property.first == "Enabled")
        {
            const bool* intfEnable = std::get_if<bool>(&property.second);
            if (intfEnable != nullptr)
            {
                if (protocol == "ipv4")
                {
                    ipv4Address.isActive = *intfEnable;
                }
                else
                {
                    ipv6Address.isActive = *intfEnable;
                }
            }
        }
    }
}

inline void handleEthInterface(
    const std::string& ifaceName,
    const dbus::utility::DBusPropertiesMap& properties,
    EthernetInterfaceData& ethData, std::vector<IPv4AddressData>& ipv4Config,
    std::vector<IPv6AddressData>& ipv6Config)
{
    if (ifaceName == "xyz.openbmc_project.Network.MACAddress")
    {
        for (const auto& property : properties)
        {
            if (property.first == "MACAddress")
            {
                const std::string* mac =
                    std::get_if<std::string>(&property.second);
                if (mac != nullptr)
                {
                    ethData.macAddress = *mac;
                }
            }
        }
    }
    else if (ifaceName == "xyz.openbmc_project.Network.EthernetInterface")
    {
        const std::string* dhcp = nullptr;
        const std::string* defaultGateway6 = nullptr;

        bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "DHCPEnabled", dhcp,
            "DefaultGateway6", defaultGateway6);
        if (!success)
        {
            BMCWEB_LOG_INFO(
                "Failed to fetch dbus properties of Hypervisor Ethernet Interface");
            return;
        }

        if (dhcp != nullptr)
        {
            ethData.dhcpEnabled = *dhcp;

            // Update origin in first created config entry
            if (!ipv4Config.empty())
            {
                ipv4Config.back().origin =
                    translateDhcpEnabledToBool(*dhcp, true) ? "DHCP" : "Static";
            }

            if (!ipv6Config.empty())
            {
                if (!translateDhcpEnabledToBool(*dhcp, false))
                {
                    ipv6Config.back().origin =
                        translateSlaacEnabledToBool(*dhcp) ? "SLAAC" : "Static";
                }
                else
                {
                    ipv6Config.back().origin = "DHCP";
                }
            }
        }

        if (defaultGateway6 != nullptr)
        {
            ethData.ipv6DefaultGateway =
                defaultGateway6->empty() ? "::" : *defaultGateway6;
        }
    }
}

inline void handleSystemConfig(
    const std::string& ifaceName,
    const dbus::utility::DBusPropertiesMap& properties,
    EthernetInterfaceData& ethData)
{
    if (ifaceName == "xyz.openbmc_project.Network.SystemConfiguration")
    {
        const std::string* hostname = nullptr;
        const std::string* defaultGateway = nullptr;
        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "HostName", hostname,
            "DefaultGateway", defaultGateway);

        if (!success)
        {
            BMCWEB_LOG_INFO(
                "Failed to fetch dbus properties of Hypervisor system config");
            return;
        }

        if (hostname != nullptr)
        {
            ethData.hostName = *hostname;
        }
        if (defaultGateway != nullptr)
        {
            ethData.defaultGateway = *defaultGateway;
        }
    }
}

inline bool extractHypervisorInterfaceData(
    const std::string& ethIfaceId,
    const dbus::utility::ManagedObjectType& dbusData,
    EthernetInterfaceData& ethData, std::vector<IPv4AddressData>& ipv4Config,
    std::vector<IPv6AddressData>& ipv6Config)
{
    bool idFound = false;

    for (const auto& objpath : dbusData)
    {
        for (const auto& ifacePair : objpath.second)
        {
            for (const std::string protocol : {"ipv4", "ipv6"})
            {
                std::string ipAddrObj = std::format(
                    "/xyz/openbmc_project/network/hypervisor/{}/{}/addr0",
                    ethIfaceId, protocol);

                if (objpath.first == ipAddrObj)
                {
                    idFound = true;
                    IPv4AddressData& ipv4Address = ipv4Config.emplace_back();
                    IPv6AddressData& ipv6Address = ipv6Config.emplace_back();

                    if (ifacePair.first == "xyz.openbmc_project.Network.IP")
                    {
                        if (!extractIpInterfaceData(ifacePair.second, protocol,
                                                    ipv4Address, ipv6Address))
                        {
                            return false;
                        }
                    }
                    else if (ifacePair.first ==
                             "xyz.openbmc_project.Object.Enable")
                    {
                        handleEnableProperty(ifacePair.second, protocol,
                                             ipv4Address, ipv6Address);
                    }
                }
            }

            if (objpath.first ==
                std::format("/xyz/openbmc_project/network/hypervisor/{}",
                            ethIfaceId))
            {
                idFound = true;
                handleEthInterface(ifacePair.first, ifacePair.second, ethData,
                                   ipv4Config, ipv6Config);
            }

            if (objpath.first ==
                "/xyz/openbmc_project/network/hypervisor/config")
            {
                handleSystemConfig(ifacePair.first, ifacePair.second, ethData);
            }
        }
    }

    return idFound;
}

/**
 * Function that retrieves all properties for given Hypervisor Ethernet
 * Interface Object from Settings Manager
 * @param ethIfaceId Hypervisor ethernet interface id to query on DBus
 * @param callback a function that shall be called to convert Dbus output
 * into JSON
 */
template <typename CallbackFunc>
void getHypervisorIfaceData(const std::string& ethIfaceId,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            CallbackFunc&& callback)
{
    sdbusplus::message::object_path path(
        "/xyz/openbmc_project/network/hypervisor");
    dbus::utility::getManagedObjects(
        "xyz.openbmc_project.Network.Hypervisor", path,
        [ethIfaceId, asyncResp,
         callback = std::forward<CallbackFunc>(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::ManagedObjectType& resp) {
            EthernetInterfaceData ethData{};
            std::vector<IPv4AddressData> ipv4Data;
            std::vector<IPv6AddressData> ipv6Data;
            if (ec)
            {
                callback(false, ethData, ipv4Data, ipv6Data);
                return;
            }

            bool found = extractHypervisorInterfaceData(
                ethIfaceId, resp, ethData, ipv4Data, ipv6Data);
            if (!found)
            {
                BMCWEB_LOG_WARNING("Hypervisor Interface not found");
                messages::resourceNotFound(asyncResp->res, "System",
                                           "hypervisor");
                return;
            }
            callback(found, ethData, ipv4Data, ipv6Data);
        });
}

/**
 * @brief Deletes given IPv4/v6 interface
 *
 * @param[in] ifaceId     Id of interface whose IP should be deleted
 * @param[in] protocol    Protocol (ipv4/ipv6)
 * @param[io] asyncResp   Response object that will be returned to client
 *
 * @return None
 */
inline void deleteHypervisorIP(
    const std::string& ifaceId, const std::string& protocol,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, ifaceId](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Internal error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            /*boost::urls::url url = boost::urls::format(
                "/redfish/v1/Systems/hypervisor/EthernetInterfaces/eth{}",
                std::to_string(ifaceId.back()));
            std::string eventOrigin = url.buffer();
            redfish::EventServiceManager::getInstance().sendEvent(
                redfish::messages::resourceChanged(), eventOrigin,
                "EthernetInterface");*/
        },
        "xyz.openbmc_project.Network.Hypervisor",
        std::format("/xyz/openbmc_project/network/hypervisor/{}/{}/addr0",
                    ifaceId, protocol),
        "xyz.openbmc_project.Object.Delete", "Delete");
}

inline void parseInterfaceData(nlohmann::json& jsonResponse,
                               const std::string& ifaceId,
                               const EthernetInterfaceData& ethData,
                               const std::vector<IPv4AddressData>& ipv4Data,
                               const std::vector<IPv6AddressData>& ipv6Data)
{
    jsonResponse["Id"] = ifaceId;
    jsonResponse["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/hypervisor/EthernetInterfaces/{}", ifaceId);
    if (ethData.macAddress)
    {
        jsonResponse["MACAddress"] = *ethData.macAddress;
    }

    jsonResponse["HostName"] = ethData.hostName;
    jsonResponse["DHCPv4"]["DHCPEnabled"] =
        translateDhcpEnabledToBool(ethData.dhcpEnabled, true);
    jsonResponse["StatelessAddressAutoConfig"]["IPv6AutoConfigEnabled"] =
        translateDHCPEnabledToIPv6AutoConfig(ethData.dhcpEnabled);

    jsonResponse["DHCPv6"]["OperatingMode"] = "Enabled";
    if (!translateDhcpEnabledToBool(ethData.dhcpEnabled, false))
    {
        jsonResponse["DHCPv6"]["OperatingMode"] = "Disabled";
    }

    nlohmann::json& ipv4Array = jsonResponse["IPv4Addresses"];
    nlohmann::json& ipv4StaticArray = jsonResponse["IPv4StaticAddresses"];
    ipv4Array = nlohmann::json::array();
    ipv4StaticArray = nlohmann::json::array();
    bool ipv4IsActive = false;

    for (const auto& ipv4Config : ipv4Data)
    {
        if (ipv4Config.isActive)
        {
            ipv4IsActive = ipv4Config.isActive;
        }
        if (!ipv4Config.address.empty())
        {
            nlohmann::json::object_t ipv4;
            ipv4["AddressOrigin"] = ipv4Config.origin;
            ipv4["SubnetMask"] = ipv4Config.netmask;
            ipv4["Address"] = ipv4Config.address;
            ipv4["Gateway"] = ipv4Config.gateway;

            if (ipv4Config.origin == "Static")
            {
                ipv4StaticArray.emplace_back(ipv4);
            }
            ipv4Array.emplace_back(std::move(ipv4));
        }
    }

    std::string ipv6GatewayStr = ethData.ipv6DefaultGateway;
    if (ipv6GatewayStr.empty())
    {
        ipv6GatewayStr = "::";
    }

    nlohmann::json& ipv6StaticDefaultGw =
        jsonResponse["IPv6StaticDefaultGateways"];
    ipv6StaticDefaultGw = nlohmann::json::array();
    nlohmann::json& ipv6Array = jsonResponse["IPv6Addresses"];
    nlohmann::json& ipv6StaticArray = jsonResponse["IPv6StaticAddresses"];
    ipv6Array = nlohmann::json::array();
    ipv6StaticArray = nlohmann::json::array();
    nlohmann::json& ipv6AddrPolicyTable =
        jsonResponse["IPv6AddressPolicyTable"];
    ipv6AddrPolicyTable = nlohmann::json::array();
    bool ipv6IsActive = false;

    for (const auto& ipv6Config : ipv6Data)
    {
        if (ipv6Config.isActive)
        {
            ipv6IsActive = ipv6Config.isActive;
        }
        if (!ipv6Config.address.empty())
        {
            nlohmann::json::object_t ipv6;
            ipv6["AddressOrigin"] = ipv6Config.origin;
            ipv6["PrefixLength"] = ipv6Config.prefixLength;
            ipv6["Address"] = ipv6Config.address;

            if (ipv6Config.origin == "Static")
            {
                nlohmann::json::object_t ipv6Static;
                ipv6Static["Address"] = ipv6["Address"];
                ipv6Static["PrefixLength"] = ipv6["PrefixLength"];
                ipv6StaticArray.emplace_back(ipv6Static);
                if (ipv6GatewayStr != "::")
                {
                    nlohmann::json::object_t ipv6StaticDefaultGwObj;
                    ipv6StaticDefaultGwObj["Address"] = ipv6GatewayStr;
                    ipv6StaticDefaultGw.emplace_back(ipv6StaticDefaultGwObj);
                }
            }
            ipv6Array.emplace_back(std::move(ipv6));
        }
        jsonResponse["IPv6DefaultGateway"] = ipv6GatewayStr;
    }

    if (ipv4IsActive)
    {
        jsonResponse["InterfaceEnabled"] = true;
    }
    else
    {
        jsonResponse["InterfaceEnabled"] = false;
        if (ipv6IsActive)
        {
            jsonResponse["InterfaceEnabled"] = true;
        }
    }
}

inline void setIpv6DhcpOperatingMode(
    const std::string& ifaceId, const EthernetInterfaceData& ethData,
    const std::string& operatingMode,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (operatingMode != "Enabled" && operatingMode != "Disabled")
    {
        messages::propertyValueIncorrect(asyncResp->res, "OperatingMode",
                                         operatingMode);
        return;
    }
    std::string ipv6DHCP;
    if (ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4" ||
        ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.both" ||
        ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4v6stateless")
    {
        if (operatingMode == "Enabled")
        {
            ipv6DHCP =
                "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.both";
        }
        else if (operatingMode == "Disabled")
        {
            ipv6DHCP =
                "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4";
        }
    }
    else if (
        ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.none" ||
        ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6" ||
        ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6stateless")
    {
        if (operatingMode == "Enabled")
        {
            ipv6DHCP =
                "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6";
        }
        else if (operatingMode == "Disabled")
        {
            ipv6DHCP =
                "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.none";
        }
    }

    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor/" + ifaceId,
        "xyz.openbmc_project.Network.EthernetInterface", "DHCPEnabled",
        ipv6DHCP, [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Internal error. ec: {}", ec);
                messages::internalError(asyncResp->res);
            }
        });
}

inline void setDHCPEnabled(const std::string& ifaceId,
                           const EthernetInterfaceData& ethData,
                           bool ipv4DHCPEnabled,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string ipv4DHCP;
    if (ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6" ||
        ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.both")
    {
        if (ipv4DHCPEnabled)
        {
            ipv4DHCP =
                "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.both";
        }
        else
        {
            ipv4DHCP =
                "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6";
        }
    }
    else if (
        ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.none" ||
        ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4")
    {
        if (ipv4DHCPEnabled)
        {
            ipv4DHCP =
                "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4";
        }
        else
        {
            ipv4DHCP =
                "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.none";
        }
    }
    else if (
        ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6stateless" ||
        ethData.dhcpEnabled ==
            "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4v6stateless")
    {
        if (ipv4DHCPEnabled)
        {
            ipv4DHCP =
                "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4v6stateless";
        }
        else
        {
            ipv4DHCP =
                "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6stateless";
        }
    }
    setDbusProperty(asyncResp, "DHCPEnabled",
                    "xyz.openbmc_project.Network.Hypervisor",
                    sdbusplus::message::object_path(
                        "/xyz/openbmc_project/network/hypervisor/" + ifaceId),
                    "xyz.openbmc_project.Network.EthernetInterface",
                    "DHCPEnabled", ipv4DHCP);
}

/**
 * @brief Creates a static IPv4/v6 entry
 *
 * @param[in] ifaceId      Id of interface upon which to create the IPv4/v6
 * entry
 * @param[in] prefixLength IPv4/v6 prefix length
 * @param[in] gateway      IPv4/v6 address of this interfaces gateway
 * @param[in] address      IPv4/v6 address to assign to this interface
 * @param[io] asyncResp    Response object that will be returned to client
 *
 * @return None
 */
inline void createHypervisorIP(
    const std::string& ifaceId, const uint8_t prefixLength,
    const std::string& gateway, const std::string& address,
    const std::string& protocol,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, ifaceId, address](const boost::system::error_code& ec) {
            if (ec)
            {
                if ((ec == boost::system::errc::invalid_argument) ||
                    (ec == boost::system::errc::argument_list_too_long))
                {
                    BMCWEB_LOG_WARNING(
                        "Static IP configuration failed. Invalid argument/Argument list too long. ec: {}, ec.value: {}",
                        ec.message(), ec.value());
                    messages::invalidObject(
                        asyncResp->res,
                        boost::urls::format(
                            "/redfish/v1/Systems/hypervisor/EthernetInterfaces/{}",
                            ifaceId));
                }
                else if (ec == boost::system::errc::io_error)
                {
                    BMCWEB_LOG_WARNING(
                        "Static IP configuration failed. ec: {}, ec.value: {}",
                        ec.message(), ec.value());
                    messages::propertyValueFormatError(asyncResp->res, address,
                                                       "Address");
                }
                else
                {
                    BMCWEB_LOG_ERROR(
                        "Static IP configuration failed with internal error. ec: {}, ec.value: {}",
                        ec.message(), ec.value());
                    messages::internalError(asyncResp->res);
                }

                return;
            }
        },
        "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor/" + ifaceId,
        "xyz.openbmc_project.Network.IP.Create", "IP", protocol, address,
        prefixLength, gateway);
}

inline void handleHypervisorIPv4StaticPatch(
    const std::string& clientIp, const std::string& ifaceId,
    const nlohmann::json& input, const EthernetInterfaceData& ethData,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if ((!input.is_array()) || input.empty())
    {
        messages::propertyValueTypeError(asyncResp->res, input,
                                         "IPv4StaticAddresses");
        return;
    }

    // Hypervisor considers the first IP address in the array list
    // as the Hypervisor's virtual management interface supports single IPv4
    // address
    const nlohmann::json& thisJson = input[0];

    if (!thisJson.is_null() && !thisJson.empty())
    {
        // For the error string
        std::string pathString = "IPv4StaticAddresses/1";
        std::string address;
        std::string subnetMask;
        std::string gateway;
        nlohmann::json thisJsonCopy = thisJson;
        if (!json_util::readJson(thisJsonCopy, asyncResp->res, "Address",
                                 address, "SubnetMask", subnetMask, "Gateway",
                                 gateway))
        {
            return;
        }

        uint8_t prefixLength = 0;
        bool errorInEntry = false;
        if (!ip_util::ipv4VerifyIpAndGetBitcount(address))
        {
            messages::propertyValueFormatError(asyncResp->res, address,
                                               pathString + "/Address");
            errorInEntry = true;
        }
        if (!ip_util::ipv4VerifyIpAndGetBitcount(subnetMask, &prefixLength))
        {
            messages::propertyValueFormatError(asyncResp->res, subnetMask,
                                               pathString + "/SubnetMask");
            errorInEntry = true;
        }
        if (!ip_util::ipv4VerifyIpAndGetBitcount(gateway))
        {
            messages::propertyValueFormatError(asyncResp->res, gateway,
                                               pathString + "/Gateway");
            errorInEntry = true;
        }

        if (errorInEntry)
        {
            return;
        }

        BMCWEB_LOG_DEBUG(
            "INFO: Static ip configuration request from client: {} - IP Address: {}, Gateway: {}, Prefix Length: {}",
            clientIp, address, gateway, static_cast<int64_t>(prefixLength));
        // Set the DHCPEnabled to false since the Static IPv4 is set
        setDHCPEnabled(ifaceId, ethData, false, asyncResp);
        BMCWEB_LOG_DEBUG("Calling createHypervisorIP on : {},{}", ifaceId,
                         address);
        createHypervisorIP(ifaceId, prefixLength, gateway, address,
                           "xyz.openbmc_project.Network.IP.Protocol.IPv4",
                           asyncResp);
    }
    else
    {
        if (thisJson.is_null())
        {
            deleteHypervisorIP(ifaceId, "ipv4", asyncResp);
        }
    }
}

inline void handleHypervisorIPv6StaticPatch(
    const crow::Request& req, const std::string& ifaceId,
    const nlohmann::json& input, const EthernetInterfaceData& ethData,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if ((!input.is_array()) || input.empty())
    {
        messages::propertyValueTypeError(asyncResp->res, input,
                                         "IPv6StaticAddresses");
        return;
    }

    // Hypervisor considers the first IP address in the array list
    // as the Hypervisor's virtual management interface supports single IPv4
    // address
    const nlohmann::json& thisJson = input[0];

    if (!thisJson.is_null() && !thisJson.empty())
    {
        // For the error string
        std::string pathString = "IPv6StaticAddresses/1";
        std::optional<std::string> address;
        std::optional<uint8_t> prefixLen;
        std::optional<std::string> gateway;
        nlohmann::json thisJsonCopy = thisJson;
        if (!json_util::readJson(thisJsonCopy, asyncResp->res, "Address",
                                 address, "PrefixLength", prefixLen, "Gateway",
                                 gateway))
        {
            return;
        }

        if (!address)
        {
            messages::propertyMissing(asyncResp->res, pathString + "/Address");
            return;
        }

        if (!gateway)
        {
            // Since gateway is optional, replace it with default value
            gateway = "::";
        }

        if (!prefixLen)
        {
            messages::propertyMissing(asyncResp->res,
                                      pathString + "/PrefixLength");
            return;
        }

        BMCWEB_LOG_INFO(
            "Static ip configuration request from client: {} - IP Address: {}, Gateway: {}, Prefix Length: {}",
            req.session->clientIp, *address, *gateway,
            static_cast<int64_t>(*prefixLen));
        // Set the DHCPEnabled to false since the Static IPv6 is set
        setIpv6DhcpOperatingMode(ifaceId, ethData, "Disabled", asyncResp);
        createHypervisorIP(ifaceId, *prefixLen, *gateway, *address,
                           "xyz.openbmc_project.Network.IP.Protocol.IPv6",
                           asyncResp);
    }
    else
    {
        if (thisJson.is_null())
        {
            deleteHypervisorIP(ifaceId, "ipv6", asyncResp);
        }
    }
}

inline void handleHypervisorV6DefaultGatewayPatch(
    const std::string& ifaceId, const std::string& ipv6DefaultGateway,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    setDbusProperty(asyncResp, "IPv6StaticDefaultGateways",
                    "xyz.openbmc_project.Network.Hypervisor",
                    sdbusplus::message::object_path(
                        "/xyz/openbmc_project/network/hypervisor/" + ifaceId),
                    "xyz.openbmc_project.Network.EthernetInterface",
                    "DefaultGateway6", ipv6DefaultGateway);
}

inline void handleHypervisorHostnamePatch(
    const std::string& hostName,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!isHostnameValid(hostName))
    {
        messages::propertyValueFormatError(asyncResp->res, hostName,
                                           "HostName");
        return;
    }

    asyncResp->res.jsonValue["HostName"] = hostName;
    setDbusProperty(asyncResp, "HostName",
                    "xyz.openbmc_project.Network.Hypervisor",
                    sdbusplus::message::object_path(
                        "/xyz/openbmc_project/network/hypervisor/config"),
                    "xyz.openbmc_project.Network.SystemConfiguration",
                    "HostName", hostName);
}

inline void handleHypervisorSLAACAutoConfigPatch(
    const std::string& ifaceId, const EthernetInterfaceData& ethData,
    const bool& ipv6AutoConfigEnabled,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const std::string dhcp = translateIPv6AutoConfigToDHCPEnabled(
        ethData.dhcpEnabled, ipv6AutoConfigEnabled);

    setDbusProperty(
        asyncResp, "StatelessAddressAutoConfig/IPv6AutoConfigEnabled",
        "xyz.openbmc_project.Network.Hypervisor",
        sdbusplus::message::object_path(
            "/xyz/openbmc_project/network/hypervisor/" + ifaceId),
        "xyz.openbmc_project.Network.EthernetInterface", "DHCPEnabled", dhcp);
}

inline void setIPv4InterfaceEnabled(
    const std::string& ifaceId, bool isActive,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    setDbusProperty(
        asyncResp, "InterfaceEnabled", "xyz.openbmc_project.Network.Hypervisor",
        std::format("/xyz/openbmc_project/network/hypervisor/{}/ipv4/addr0",
                    ifaceId),
        "xyz.openbmc_project.Object.Enable", "Enabled", isActive);
}

inline void handleHypervisorEthernetInterfaceCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Network.EthernetInterface"};

    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/network/hypervisor", 0, interfaces,
        [asyncResp](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& ifaceList) {
            asyncResp->res.jsonValue["@odata.type"] =
                "#EthernetInterfaceCollection."
                "EthernetInterfaceCollection";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/hypervisor/EthernetInterfaces";
            asyncResp->res.jsonValue["Name"] = "Hypervisor Ethernet "
                                               "Interface Collection";
            asyncResp->res.jsonValue["Description"] =
                "Collection of Virtual Management "
                "Interfaces for the hypervisor";

            nlohmann::json& ifaceArray = asyncResp->res.jsonValue["Members"];
            ifaceArray = nlohmann::json::array();
            if (ec)
            {
                BMCWEB_LOG_WARNING("DBUS error {}", ec);
                asyncResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }
            for (const std::string& iface : ifaceList)
            {
                sdbusplus::message::object_path path(iface);
                std::string name = path.filename();
                if (name.empty())
                {
                    continue;
                }
                nlohmann::json::object_t ethIface;
                ethIface["@odata.id"] = boost::urls::format(
                    "/redfish/v1/Systems/hypervisor/EthernetInterfaces/{}",
                    name);
                ifaceArray.emplace_back(std::move(ethIface));
            }
            asyncResp->res.jsonValue["Members@odata.count"] = ifaceArray.size();
        });
}

inline void handleHypervisorEthernetInterfaceGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& id)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    getHypervisorIfaceData(
        id, asyncResp,
        [asyncResp, ifaceId{std::string(id)}](
            bool success, const EthernetInterfaceData& ethData,
            const std::vector<IPv4AddressData>& ipv4Data,
            const std::vector<IPv6AddressData>& ipv6Data) {
            if (!success)
            {
                messages::resourceNotFound(asyncResp->res, "EthernetInterface",
                                           ifaceId);
                return;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#EthernetInterface.v1_9_0.EthernetInterface";
            asyncResp->res.jsonValue["Name"] = "Hypervisor Ethernet Interface";
            asyncResp->res.jsonValue["Description"] =
                "Hypervisor's Virtual Management Ethernet Interface";
            parseInterfaceData(asyncResp->res.jsonValue, ifaceId, ethData,
                               ipv4Data, ipv6Data);
        });
}

inline void handleHypervisorSystemGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getProperty<std::string>(
        "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor/config",
        "xyz.openbmc_project.Network.SystemConfiguration", "HostName",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& /*hostName*/) {
            if (ec)
            {
                BMCWEB_LOG_WARNING("Resource not found: hypervisor");
                messages::resourceNotFound(asyncResp->res, "System",
                                           "hypervisor");
                return;
            }
            BMCWEB_LOG_DEBUG("Hypervisor is available");

            asyncResp->res.jsonValue["@odata.type"] =
                "#ComputerSystem.v1_6_0.ComputerSystem";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/hypervisor";
            asyncResp->res.jsonValue["Description"] = "Hypervisor";
            asyncResp->res.jsonValue["Name"] = "Hypervisor";
            asyncResp->res.jsonValue["Id"] = "hypervisor";
            asyncResp->res.jsonValue["SystemType"] =
                computer_system::SystemType::OS;
            nlohmann::json::array_t managedBy;
            nlohmann::json::object_t manager;
            manager["@odata.id"] = boost::urls::format(
                "/redfish/v1/Managers/{}", BMCWEB_REDFISH_MANAGER_URI_NAME);
            managedBy.emplace_back(std::move(manager));
            asyncResp->res.jsonValue["Links"]["ManagedBy"] =
                std::move(managedBy);
            asyncResp->res.jsonValue["EthernetInterfaces"]["@odata.id"] =
                "/redfish/v1/Systems/hypervisor/EthernetInterfaces";
            getHypervisorState(asyncResp);
            getHypervisorActions(asyncResp);
            // TODO: Add "SystemType" : "hypervisor"
        });
}

inline void handleHypervisorPatchFromIfaceData(
    const crow::Request& req, const std::string& clientIp,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ifaceId, const std::optional<std::string>& hostName,
    const std::optional<std::vector<nlohmann::json>>& ipv4StaticAddresses,
    const std::optional<std::vector<nlohmann::json>>& ipv6StaticAddresses,
    const std::optional<bool>& ipv4DHCPEnabled,
    const std::optional<std::string>& ipv6OperatingMode,
    const std::optional<nlohmann::json::array_t>& ipv6StaticDefaultGateways,
    const std::optional<bool>& ipv6AutoConfigEnabled, const bool& success,
    const EthernetInterfaceData& ethData)
{
    if (!success)
    {
        messages::resourceNotFound(asyncResp->res, "EthernetInterface",
                                   ifaceId);
        return;
    }

    if (ipv4StaticAddresses)
    {
        const nlohmann::json& ipv4Static = *ipv4StaticAddresses;
        if (ipv4Static.empty())
        {
            messages::propertyValueTypeError(asyncResp->res, ipv4Static,
                                             "IPv4StaticAddresses");
            return;
        }

        if (ipv4Static.size() != 1)
        {
            messages::propertyValueFormatError(asyncResp->res, ipv4Static,
                                               "IPv4StaticAddresses");
            return;
        }

        const nlohmann::json& ipv4Json = ipv4Static[0];
        if (ipv4Json.is_null() &&
            translateDhcpEnabledToBool(ethData.dhcpEnabled, true))
        {
            BMCWEB_LOG_INFO("Skipping delete: DHCP enabled on interface");
        }
        else
        {
            handleHypervisorIPv4StaticPatch(clientIp, ifaceId, ipv4Static,
                                            ethData, asyncResp);
        }
    }

    if (ipv6StaticAddresses)
    {
        const nlohmann::json& ipv6Static = *ipv6StaticAddresses;
        if (ipv6Static.empty())
        {
            messages::propertyValueTypeError(asyncResp->res, ipv6Static,
                                             "IPv6StaticAddresses");
            return;
        }

        if (ipv6Static.size() != 1)
        {
            messages::propertyValueFormatError(asyncResp->res, ipv6Static,
                                               "IPv6StaticAddresses");
            return;
        }

        const nlohmann::json& ipv6Json = ipv6Static[0];
        if (ipv6Json.is_null() &&
            translateDhcpEnabledToBool(ethData.dhcpEnabled, false))
        {
            BMCWEB_LOG_INFO("Skipping delete: DHCP enabled on interface");
        }
        else
        {
            handleHypervisorIPv6StaticPatch(req, ifaceId, ipv6Static, ethData,
                                            asyncResp);
        }
    }

    if (hostName)
    {
        handleHypervisorHostnamePatch(*hostName, asyncResp);
    }

    if (ipv4DHCPEnabled)
    {
        setDHCPEnabled(ifaceId, ethData, *ipv4DHCPEnabled, asyncResp);
    }

    if (ipv6OperatingMode)
    {
        setIpv6DhcpOperatingMode(ifaceId, ethData, *ipv6OperatingMode,
                                 asyncResp);
    }

    if (ipv6AutoConfigEnabled)
    {
        handleHypervisorSLAACAutoConfigPatch(ifaceId, ethData,
                                             *ipv6AutoConfigEnabled, asyncResp);
    }

    if (ipv6StaticDefaultGateways)
    {
        if (ipv6StaticDefaultGateways->empty())
        {
            BMCWEB_LOG_ERROR("IPv6 Default Gateway property is empty");
            messages::invalidObject(
                asyncResp->res,
                boost::urls::format(
                    "/redfish/v1/Systems/hypervisor/EthernetInterfaces/{}",
                    ifaceId));
            return;
        }

        if (ipv6StaticDefaultGateways->size() > 1)
        {
            messages::propertyValueModified(asyncResp->res,
                                            "IPv6StaticDefaultGateways",
                                            ipv6StaticDefaultGateways->front());
        }

        const auto& firstGateway = ipv6StaticDefaultGateways->front();
        handleHypervisorV6DefaultGatewayPatch(
            ifaceId, firstGateway.is_null() ? "::" : firstGateway, asyncResp);
    }
}

inline void handleHypervisorEthernetInterfacePatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ifaceId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::optional<std::string> hostName;
    std::optional<std::vector<nlohmann::json>> ipv4StaticAddresses;
    std::optional<std::vector<nlohmann::json>> ipv6StaticAddresses;
    std::optional<nlohmann::json> ipv4Addresses;
    std::optional<bool> ipv4DHCPEnabled;
    std::optional<std::string> ipv6OperatingMode;
    std::optional<bool> ipv6AutoConfigEnabled;
    std::optional<nlohmann::json::array_t> ipv6StaticDefaultGateways;

    if (!json_util::readJsonPatch(
            req, asyncResp->res,                                   //
            "HostName", hostName,                                  //
            "IPv4StaticAddresses", ipv4StaticAddresses,            //
            "IPv6StaticAddresses", ipv6StaticAddresses,            //
            "IPv4Addresses", ipv4Addresses,                        //
            "DHCPv4/DHCPEnabled", ipv4DHCPEnabled,                 //
            "DHCPv6/OperatingMode", ipv6OperatingMode,             //
            "StatelessAddressAutoConfig/IPv6AutoConfigEnabled",
            ipv6AutoConfigEnabled,                                 //
            "IPv6StaticDefaultGateways", ipv6StaticDefaultGateways //
            ))
    {
        return;
    }

    if (ipv4Addresses)
    {
        messages::propertyNotWritable(asyncResp->res, "IPv4Addresses");
        return;
    }

    const std::string& clientIp = req.session->clientIp;
    getHypervisorIfaceData(
        ifaceId, asyncResp, //
        [req, clientIp, asyncResp, ifaceId, hostName = std::move(hostName),
         ipv4StaticAddresses = std::move(ipv4StaticAddresses),
         ipv6StaticAddresses = std::move(ipv6StaticAddresses), ipv4DHCPEnabled,
         ipv6OperatingMode,
         ipv6StaticDefaultGateways = std::move(ipv6StaticDefaultGateways),
         ipv6AutoConfigEnabled](const bool& success,
                                const EthernetInterfaceData& ethData,
                                const std::vector<IPv4AddressData>&,
                                const std::vector<IPv6AddressData>&) {
            handleHypervisorPatchFromIfaceData(
                req, clientIp, asyncResp, ifaceId, hostName,
                ipv4StaticAddresses, ipv6StaticAddresses, ipv4DHCPEnabled,
                ipv6OperatingMode, ipv6StaticDefaultGateways,
                ipv6AutoConfigEnabled, success, ethData);
        });
    asyncResp->res.result(boost::beast::http::status::accepted);
}

inline void handleHypervisorResetActionGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Only return action info if hypervisor D-Bus object present
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.State.Host"};
    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/state/hypervisor0", interfaces,
        [asyncResp](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);

                // No hypervisor objects found by mapper
                if (ec.value() == boost::system::errc::io_error)
                {
                    messages::resourceNotFound(asyncResp->res, "hypervisor",
                                               "ResetActionInfo");
                    return;
                }

                messages::internalError(asyncResp->res);
                return;
            }

            // One and only one hypervisor instance supported
            if (objInfo.size() != 1)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            // The hypervisor object only support the ability to
            // turn On The system object Action should be utilized
            // for other operations

            asyncResp->res.jsonValue["@odata.type"] =
                "#ActionInfo.v1_1_2.ActionInfo";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/hypervisor/ResetActionInfo";
            asyncResp->res.jsonValue["Name"] = "Reset Action Info";
            asyncResp->res.jsonValue["Id"] = "ResetActionInfo";
            nlohmann::json::array_t parameters;
            nlohmann::json::object_t parameter;
            parameter["Name"] = "ResetType";
            parameter["Required"] = true;
            parameter["DataType"] = action_info::ParameterTypes::String;
            nlohmann::json::array_t allowed;
            allowed.emplace_back("On");
            parameter["AllowableValues"] = std::move(allowed);
            parameters.emplace_back(std::move(parameter));
            asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
        });
}

inline void handleHypervisorSystemResetPost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::optional<std::string> resetType;
    if (!json_util::readJsonAction(req, asyncResp->res, "ResetType", resetType))
    {
        // readJson adds appropriate error to response
        return;
    }

    if (!resetType)
    {
        messages::actionParameterMissing(asyncResp->res, "ComputerSystem.Reset",
                                         "ResetType");
        return;
    }

    // Hypervisor object only support On operation
    if (resetType != "On")
    {
        messages::propertyValueNotInList(asyncResp->res, *resetType,
                                         "ResetType");
        return;
    }

    std::string command = "xyz.openbmc_project.State.Host.Transition.On";

    setDbusPropertyAction(
        asyncResp, "xyz.openbmc_project.State.Hypervisor",
        sdbusplus::message::object_path(
            "/xyz/openbmc_project/state/hypervisor0"),
        "xyz.openbmc_project.State.Host", "RequestedHostTransition",
        "ResetType", "ComputerSystem.Reset", command);
}

inline void requestRoutesHypervisorSystems(App& app)
{
    /**
     * HypervisorInterfaceCollection class to handle the GET and PATCH on
     * Hypervisor Interface
     */

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/hypervisor/EthernetInterfaces/")
        .privileges(redfish::privileges::getEthernetInterfaceCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleHypervisorEthernetInterfaceCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/hypervisor/EthernetInterfaces/<str>/")
        .privileges(redfish::privileges::getEthernetInterface)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleHypervisorEthernetInterfaceGet, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/hypervisor/EthernetInterfaces/<str>/")
        .privileges(redfish::privileges::patchEthernetInterface)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            handleHypervisorEthernetInterfacePatch, std::ref(app)));
}
} // namespace redfish
