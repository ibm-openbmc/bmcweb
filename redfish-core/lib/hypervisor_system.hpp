// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
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
#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
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

inline bool extractHypervisorInterfaceData(
    const std::string& ethIfaceId,
    const dbus::utility::ManagedObjectType& dbusData,
    EthernetInterfaceData& ethData, std::vector<IPv4AddressData>& ipv4Config)
{
    bool idFound = false;
    for (const auto& objpath : dbusData)
    {
        for (const auto& ifacePair : objpath.second)
        {
            if (objpath.first ==
                "/xyz/openbmc_project/network/hypervisor/" + ethIfaceId)
            {
                idFound = true;
                if (ifacePair.first ==
                    "xyz.openbmc_project.Network.EthernetInterface")
                {
                    for (const auto& propertyPair : ifacePair.second)
                    {
                        if (propertyPair.first == "DHCPEnabled")
                        {
                            const std::string* dhcp =
                                std::get_if<std::string>(&propertyPair.second);
                            if (dhcp != nullptr)
                            {
                                ethData.dhcpEnabled = *dhcp;
                                break; // Interested on only "DHCPEnabled".
                                       // Stop parsing since we got the
                                       // "DHCPEnabled" value.
                            }
                        }
                    }
                }
            }
            if (objpath.first == "/xyz/openbmc_project/network/hypervisor/" +
                                     ethIfaceId + "/ipv4/addr0")
            {
                IPv4AddressData& ipv4Address = ipv4Config.emplace_back();
                if (ifacePair.first == "xyz.openbmc_project.Object.Enable")
                {
                    for (const auto& property : ifacePair.second)
                    {
                        if (property.first == "Enabled")
                        {
                            const bool* intfEnable =
                                std::get_if<bool>(&property.second);
                            if (intfEnable != nullptr)
                            {
                                ipv4Address.isActive = *intfEnable;
                                break;
                            }
                        }
                    }
                }
                if (ifacePair.first == "xyz.openbmc_project.Network.IP")
                {
                    for (const auto& property : ifacePair.second)
                    {
                        if (property.first == "Address")
                        {
                            const std::string* address =
                                std::get_if<std::string>(&property.second);
                            if (address != nullptr)
                            {
                                ipv4Address.address = *address;
                            }
                        }
                        else if (property.first == "Origin")
                        {
                            const std::string* origin =
                                std::get_if<std::string>(&property.second);
                            if (origin != nullptr)
                            {
                                ipv4Address.origin =
                                    translateAddressOriginDbusToRedfish(*origin,
                                                                        true);
                            }
                        }
                        else if (property.first == "PrefixLength")
                        {
                            const uint8_t* mask =
                                std::get_if<uint8_t>(&property.second);
                            if (mask != nullptr)
                            {
                                // convert it to the string
                                ipv4Address.netmask = getNetmask(*mask);
                            }
                        }
                        else if (property.first == "Type" ||
                                 property.first == "Gateway")
                        {
                            // Type & Gateway is not used
                            continue;
                        }
                        else
                        {
                            BMCWEB_LOG_ERROR(
                                "Got extra property: {} on the {} object",
                                property.first, objpath.first.str);
                        }
                    }
                }
            }
            if (objpath.first == "/xyz/openbmc_project/network/hypervisor")
            {
                // System configuration shows up in the global namespace, so no
                // need to check eth number
                if (ifacePair.first ==
                    "xyz.openbmc_project.Network.SystemConfiguration")
                {
                    for (const auto& propertyPair : ifacePair.second)
                    {
                        if (propertyPair.first == "HostName")
                        {
                            const std::string* hostName =
                                std::get_if<std::string>(&propertyPair.second);
                            if (hostName != nullptr)
                            {
                                ethData.hostName = *hostName;
                            }
                        }
                        else if (propertyPair.first == "DefaultGateway")
                        {
                            const std::string* defaultGateway =
                                std::get_if<std::string>(&propertyPair.second);
                            if (defaultGateway != nullptr)
                            {
                                ethData.defaultGateway = *defaultGateway;
                            }
                        }
                    }
                }
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
                            CallbackFunc&& callback)
{
    sdbusplus::message::object_path path("/");
    dbus::utility::getManagedObjects(
        "xyz.openbmc_project.Settings", path,
        [ethIfaceId{std::string{ethIfaceId}},
         callback = std::forward<CallbackFunc>(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::ManagedObjectType& resp) mutable {
            EthernetInterfaceData ethData{};
            std::vector<IPv4AddressData> ipv4Data;
            if (ec)
            {
                callback(false, ethData, ipv4Data);
                return;
            }

            bool found = extractHypervisorInterfaceData(ethIfaceId, resp,
                                                        ethData, ipv4Data);
            if (!found)
            {
                BMCWEB_LOG_INFO("Hypervisor Interface not found");
            }
            callback(found, ethData, ipv4Data);
        });
}

/**
 * @brief Sets the Hypervisor Interface IPAddress DBUS
 *
 * @param[in] asyncResp          Shared pointer for generating response message.
 * @param[in] ipv4Address    Address from the incoming request
 * @param[in] ethIfaceId     Hypervisor Interface Id
 *
 * @return None.
 */
inline void setHypervisorIPv4Address(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ethIfaceId, const std::string& ipv4Address)
{
    BMCWEB_LOG_DEBUG("Setting the Hypervisor IPaddress : {} on Iface: {}",
                     ipv4Address, ethIfaceId);

    setDbusProperty(
        asyncResp, "IPv4StaticAddresses/1/Address",
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/network/hypervisor/" + ethIfaceId + "/ipv4/addr0",
        "xyz.openbmc_project.Network.IP", "Address", ipv4Address);
}

/**
 * @brief Sets the Hypervisor Interface SubnetMask DBUS
 *
 * @param[in] asyncResp     Shared pointer for generating response message.
 * @param[in] subnet    SubnetMask from the incoming request
 * @param[in] ethIfaceId Hypervisor Interface Id
 *
 * @return None.
 */
inline void setHypervisorIPv4Subnet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ethIfaceId, const uint8_t subnet)
{
    BMCWEB_LOG_DEBUG("Setting the Hypervisor subnet : {} on Iface: {}", subnet,
                     ethIfaceId);

    setDbusProperty(
        asyncResp, "IPv4StaticAddresses/1/SubnetMask",
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/network/hypervisor/" + ethIfaceId + "/ipv4/addr0",
        "xyz.openbmc_project.Network.IP", "PrefixLength", subnet);
}

/**
 * @brief Sets the Hypervisor Interface Gateway DBUS
 *
 * @param[in] asyncResp          Shared pointer for generating response message.
 * @param[in] gateway        Gateway from the incoming request
 * @param[in] ethIfaceId     Hypervisor Interface Id
 *
 * @return None.
 */
inline void setHypervisorIPv4Gateway(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& gateway)
{
    BMCWEB_LOG_DEBUG(
        "Setting the DefaultGateway to the last configured gateway");

    setDbusProperty(asyncResp, "IPv4StaticAddresses/1/Gateway",
                    "xyz.openbmc_project.Settings",
                    sdbusplus::message::object_path(
                        "/xyz/openbmc_project/network/hypervisor"),
                    "xyz.openbmc_project.Network.SystemConfiguration",
                    "DefaultGateway", gateway);
}

/**
 * @brief Creates a static IPv4 entry
 *
 * @param[in] ifaceId      Id of interface upon which to create the IPv4 entry
 * @param[in] prefixLength IPv4 prefix syntax for the subnet mask
 * @param[in] gateway      IPv4 address of this interfaces gateway
 * @param[in] address      IPv4 address to assign to this interface
 * @param[io] asyncResp    Response object that will be returned to client
 *
 * @return None
 */
inline void createHypervisorIPv4(
    const std::string& ifaceId, uint8_t prefixLength,
    const std::string& gateway, const std::string& address,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    setHypervisorIPv4Address(asyncResp, ifaceId, address);
    setHypervisorIPv4Gateway(asyncResp, gateway);
    setHypervisorIPv4Subnet(asyncResp, ifaceId, prefixLength);
}

/**
 * @brief Deletes given IPv4 interface
 *
 * @param[in] ifaceId     Id of interface whose IP should be deleted
 * @param[io] asyncResp   Response object that will be returned to client
 *
 * @return None
 */
inline void deleteHypervisorIPv4(
    const std::string& ifaceId,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string address = "0.0.0.0";
    std::string gateway = "0.0.0.0";
    const uint8_t prefixLength = 0;
    setHypervisorIPv4Address(asyncResp, ifaceId, address);
    setHypervisorIPv4Gateway(asyncResp, gateway);
    setHypervisorIPv4Subnet(asyncResp, ifaceId, prefixLength);
}

inline void parseInterfaceData(nlohmann::json& jsonResponse,
                               const std::string& ifaceId,
                               const EthernetInterfaceData& ethData,
                               const std::vector<IPv4AddressData>& ipv4Data)
{
    jsonResponse["Id"] = ifaceId;
    jsonResponse["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/hypervisor/EthernetInterfaces/{}", ifaceId);
    jsonResponse["InterfaceEnabled"] = true;
    jsonResponse["HostName"] = ethData.hostName;
    jsonResponse["DHCPv4"]["DHCPEnabled"] =
        translateDhcpEnabledToBool(ethData.dhcpEnabled, true);

    nlohmann::json& ipv4Array = jsonResponse["IPv4Addresses"];
    nlohmann::json& ipv4StaticArray = jsonResponse["IPv4StaticAddresses"];
    ipv4Array = nlohmann::json::array();
    ipv4StaticArray = nlohmann::json::array();
    for (const auto& ipv4Config : ipv4Data)
    {
        if (ipv4Config.isActive)
        {
            nlohmann::json::object_t ipv4;
            ipv4["AddressOrigin"] = ipv4Config.origin;
            ipv4["SubnetMask"] = ipv4Config.netmask;
            ipv4["Address"] = ipv4Config.address;
            ipv4["Gateway"] = ethData.defaultGateway;

            if (ipv4Config.origin == "Static")
            {
                ipv4StaticArray.push_back(ipv4);
            }
            ipv4Array.emplace_back(std::move(ipv4));
        }
    }
}

inline void setDHCPEnabled(const std::string& ifaceId, bool ipv4DHCPEnabled,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const std::string dhcp = getDhcpEnabledEnumeration(ipv4DHCPEnabled, false);

    setDbusProperty(
        asyncResp, "DHCPv4/DHCPEnabled", "xyz.openbmc_project.Settings",
        sdbusplus::message::object_path(
            "/xyz/openbmc_project/network/hypervisor") /
            ifaceId,
        "xyz.openbmc_project.Network.EthernetInterface", "DHCPEnabled", dhcp);

    // Set the IPv4 address origin to the DHCP / Static as per the new value
    // of the DHCPEnabled property
    std::string origin;
    if (!ipv4DHCPEnabled)
    {
        origin = "xyz.openbmc_project.Network.IP.AddressOrigin.Static";
    }
    else
    {
        // DHCPEnabled is set to true. Delete the current IPv4 settings
        // to receive the new values from DHCP server.
        deleteHypervisorIPv4(ifaceId, asyncResp);
        origin = "xyz.openbmc_project.Network.IP.AddressOrigin.DHCP";
    }

    setDbusProperty(
        asyncResp, "IPv4StaticAddresses/1/AddressOrigin",
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/network/hypervisor/" + ifaceId + "/ipv4/addr0",
        "xyz.openbmc_project.Network.IP", "Origin", origin);
}

inline void handleHypervisorIPv4StaticPatch(
    const std::string& ifaceId,
    std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>>& input,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Hypervisor considers the first IP address in the array list
    // as the Hypervisor's virtual management interface supports single IPv4
    // address
    std::variant<nlohmann::json::object_t, std::nullptr_t>& thisJson = input[0];
    nlohmann::json::object_t* obj =
        std::get_if<nlohmann::json::object_t>(&thisJson);
    if (obj == nullptr)
    {
        deleteHypervisorIPv4(ifaceId, asyncResp);
        return;
    }
    if (obj->empty())
    {
        return;
    }
    // For the error string
    std::string pathString = "IPv4StaticAddresses/1";
    std::string address;
    std::string subnetMask;
    std::string gateway;
    if (!json_util::readJsonObject(  //
            *obj, asyncResp->res,    //
            "Address", address,      //
            "Gateway", gateway,      //
            "SubnetMask", subnetMask //
            ))
    {
        return;
    }

    uint8_t prefixLength = 0;
    if (!ip_util::ipv4VerifyIpAndGetBitcount(address))
    {
        messages::propertyValueFormatError(asyncResp->res, address,
                                           pathString + "/Address");
        return;
    }

    if (!ip_util::ipv4VerifyIpAndGetBitcount(subnetMask, &prefixLength))
    {
        messages::propertyValueFormatError(asyncResp->res, subnetMask,
                                           pathString + "/SubnetMask");
        return;
    }

    if (!ip_util::ipv4VerifyIpAndGetBitcount(gateway))
    {
        messages::propertyValueFormatError(asyncResp->res, gateway,
                                           pathString + "/Gateway");
        return;
    }

    BMCWEB_LOG_DEBUG("Calling createHypervisorIPv4 on : {},{}", ifaceId,
                     address);
    createHypervisorIPv4(ifaceId, prefixLength, gateway, address, asyncResp);
    // Set the DHCPEnabled to false since the Static IPv4 is set
    setDHCPEnabled(ifaceId, false, asyncResp);
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
    setDbusProperty(asyncResp, "HostName", "xyz.openbmc_project.Settings",
                    sdbusplus::message::object_path(
                        "/xyz/openbmc_project/network/hypervisor"),
                    "xyz.openbmc_project.Network.SystemConfiguration",
                    "HostName", hostName);
}

inline void setIPv4InterfaceEnabled(
    const std::string& ifaceId, bool isActive,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    setDbusProperty(
        asyncResp, "InterfaceEnabled", "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/network/hypervisor/" + ifaceId + "/ipv4/addr0",
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
            if (ec)
            {
                messages::resourceNotFound(asyncResp->res, "System",
                                           "hypervisor");
                return;
            }
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
        id, [asyncResp, ifaceId{std::string(id)}](
                bool success, const EthernetInterfaceData& ethData,
                const std::vector<IPv4AddressData>& ipv4Data) {
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
                               ipv4Data);
        });
}

inline void handleHypervisorSystemGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    asyncResp->res.jsonValue["@odata.type"] =
        "#ComputerSystem.v1_6_0.ComputerSystem";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Systems/hypervisor";
    asyncResp->res.jsonValue["Description"] = "Hypervisor";
    asyncResp->res.jsonValue["Name"] = "Hypervisor";
    asyncResp->res.jsonValue["Id"] = "hypervisor";
    asyncResp->res.jsonValue["SystemType"] = computer_system::SystemType::OS;
    nlohmann::json::array_t managedBy;
    nlohmann::json::object_t manager;
    manager["@odata.id"] = boost::urls::format("/redfish/v1/Managers/{}",
                                               BMCWEB_REDFISH_MANAGER_URI_NAME);
    managedBy.emplace_back(std::move(manager));
    asyncResp->res.jsonValue["Links"]["ManagedBy"] = std::move(managedBy);
    // asyncResp->res.jsonValue["EthernetInterfaces"]["@odata.id"] =
    //     "/redfish/v1/Systems/hypervisor/EthernetInterfaces";
    getHypervisorState(asyncResp);
    getHypervisorActions(asyncResp);
    // TODO: Add "SystemType" : "hypervisor"
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
    std::optional<
        std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>>>
        ipv4StaticAddresses;
    std::optional<std::vector<nlohmann::json::object_t>> ipv4Addresses;
    std::optional<bool> ipv4DHCPEnabled;

    if (!json_util::readJsonPatch(                      //
            req, asyncResp->res,                        //
            "DHCPv4/DHCPEnabled", ipv4DHCPEnabled,      //
            "IPv4Addresses", ipv4Addresses,             //
            "IPv4StaticAddresses", ipv4StaticAddresses, //
            "HostName", hostName                        //
            ))
    {
        return;
    }

    if (ipv4Addresses)
    {
        messages::propertyNotWritable(asyncResp->res, "IPv4Addresses");
        return;
    }

    getHypervisorIfaceData(
        ifaceId,
        [asyncResp, ifaceId, hostName = std::move(hostName),
         ipv4StaticAddresses = std::move(ipv4StaticAddresses),
         ipv4DHCPEnabled](bool success, const EthernetInterfaceData& ethData,
                          const std::vector<IPv4AddressData>&) mutable {
            if (!success)
            {
                messages::resourceNotFound(asyncResp->res, "EthernetInterface",
                                           ifaceId);
                return;
            }

            if (ipv4StaticAddresses)
            {
                std::vector<std::variant<nlohmann::json::object_t,
                                         std::nullptr_t>>& ipv4Static =
                    *ipv4StaticAddresses;
                if (ipv4Static.begin() == ipv4Static.end())
                {
                    messages::propertyValueTypeError(asyncResp->res,
                                                     std::vector<std::string>(),
                                                     "IPv4StaticAddresses");
                    return;
                }

                // One and only one hypervisor instance supported
                if (ipv4Static.size() != 1)
                {
                    messages::propertyValueFormatError(asyncResp->res, "[]",
                                                       "IPv4StaticAddresses");
                    return;
                }

                std::variant<nlohmann::json::object_t, std::nullptr_t>&
                    ipv4Json = ipv4Static[0];
                // Check if the param is 'null'. If its null, it means
                // that user wants to delete the IP address. Deleting
                // the IP address is allowed only if its statically
                // configured. Deleting the address originated from DHCP
                // is not allowed.
                if (std::holds_alternative<std::nullptr_t>(ipv4Json) &&
                    translateDhcpEnabledToBool(ethData.dhcpEnabled, true))
                {
                    BMCWEB_LOG_INFO(
                        "Ignoring the delete on ipv4StaticAddresses "
                        "as the interface is DHCP enabled");
                }
                else
                {
                    handleHypervisorIPv4StaticPatch(ifaceId, ipv4Static,
                                                    asyncResp);
                }
            }

            if (hostName)
            {
                handleHypervisorHostnamePatch(*hostName, asyncResp);
            }

            if (ipv4DHCPEnabled)
            {
                setDHCPEnabled(ifaceId, *ipv4DHCPEnabled, asyncResp);
            }

            // Set this interface to disabled/inactive. This will be set
            // to enabled/active by the pldm once the hypervisor
            // consumes the updated settings from the user.
            setIPv4InterfaceEnabled(ifaceId, false, asyncResp);
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
