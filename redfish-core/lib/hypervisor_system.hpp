#pragma once

#include "ethernet.hpp"
#include "utils/ip_utils.hpp"

#include <app.hpp>
#include <boost/container/flat_set.hpp>
#include <dbus_singleton.hpp>
#include <dbus_utility.hpp>
#include <error_messages.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <utils/json_utils.hpp>

#include <optional>
#include <utility>
#include <variant>

// TODO(ed) requestRoutesHypervisorSystems seems to have copy-pasted a
// lot of code, and has a number of methods that have name conflicts with the
// normal ethernet internfaces in ethernet.hpp.  For the moment, we'll put
// hypervisor in a namespace to isolate it, but these methods eventually need
// deduplicated
namespace redfish::hypervisor
{

using namespace redfish::ip_util;

/**
 * @brief Retrieves hypervisor state properties over dbus
 *
 * The hypervisor state object is optional so this function will only set the
 * state variables if the object is found
 *
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void getHypervisorState(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get hypervisor state information.";
    sdbusplus::asio::getProperty<std::variant<std::string>>(
        *crow::connections::systemBus, "xyz.openbmc_project.State.Hypervisor",
        "/xyz/openbmc_project/state/hypervisor0",
        "xyz.openbmc_project.State.Host", "CurrentHostState",
        [aResp](const boost::system::error_code ec,
                const std::variant<std::string>& hostState) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
            // This is an optional D-Bus object so just return if
            // error occurs
            return;
        }

        const std::string* s = std::get_if<std::string>(&hostState);
        if (s == nullptr)
        {
            messages::internalError(aResp->res);
            return;
        }

        BMCWEB_LOG_DEBUG << "Hypervisor state: " << *s;
        // Verify Host State
        if (*s == "xyz.openbmc_project.State.Host.HostState.Running")
        {
            aResp->res.jsonValue["PowerState"] = "On";
            aResp->res.jsonValue["Status"]["State"] = "Enabled";
        }
        else if (*s == "xyz.openbmc_project.State.Host.HostState."
                       "Quiesced")
        {
            aResp->res.jsonValue["PowerState"] = "On";
            aResp->res.jsonValue["Status"]["State"] = "Quiesced";
        }
        else if (*s == "xyz.openbmc_project.State.Host.HostState."
                       "Standby")
        {
            aResp->res.jsonValue["PowerState"] = "On";
            aResp->res.jsonValue["Status"]["State"] = "StandbyOffline";
        }
        else if (*s == "xyz.openbmc_project.State.Host.HostState."
                       "TransitioningToRunning")
        {
            aResp->res.jsonValue["PowerState"] = "PoweringOn";
            aResp->res.jsonValue["Status"]["State"] = "Starting";
        }
        else if (*s == "xyz.openbmc_project.State.Host.HostState."
                       "TransitioningToOff")
        {
            aResp->res.jsonValue["PowerState"] = "PoweringOff";
            aResp->res.jsonValue["Status"]["State"] = "Enabled";
        }
        else if (*s == "xyz.openbmc_project.State.Host.HostState.Off")
        {
            aResp->res.jsonValue["PowerState"] = "Off";
            aResp->res.jsonValue["Status"]["State"] = "Disabled";
        }
        else
        {
            messages::internalError(aResp->res);
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
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void
    getHypervisorActions(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get hypervisor actions.";
    crow::connections::systemBus->async_method_call(
        [aResp](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
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
            messages::internalError(aResp->res);
            return;
        }

        // Object present so system support limited ComputerSystem Action
        nlohmann::json& reset =
            aResp->res.jsonValue["Actions"]["#ComputerSystem.Reset"];
        reset["target"] =
            "/redfish/v1/Systems/hypervisor/Actions/ComputerSystem.Reset";
        reset["@Redfish.ActionInfo"] =
            "/redfish/v1/Systems/hypervisor/ResetActionInfo";
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        "/xyz/openbmc_project/state/hypervisor0",
        std::array<const char*, 1>{"xyz.openbmc_project.State.Host"});
}

inline bool translateSlaacEnabledToBool(const std::string& inputDHCP)
{
    return (
        (inputDHCP ==
         "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6stateless") ||
        (inputDHCP ==
         "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4v6stateless"));
}

inline bool extractHypervisorInterfaceData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ethIfaceId,
    const dbus::utility::ManagedObjectType& dbusData,
    EthernetInterfaceData& ethData,
    boost::container::flat_set<IPv4AddressData>& ipv4Config,
    boost::container::flat_set<IPv6AddressData>& ipv6Config)
{
    bool idFound = false;
    for (const auto& objpath : dbusData)
    {
        for (const auto& interface : objpath.second)
        {
            std::pair<boost::container::flat_set<IPv4AddressData>::iterator,
                      bool>
                v4Itr = ipv4Config.insert(IPv4AddressData{});

            std::pair<boost::container::flat_set<IPv6AddressData>::iterator,
                      bool>
                v6Itr = ipv6Config.insert(IPv6AddressData{});

            IPv4AddressData& ipv4Address = *v4Itr.first;
            IPv6AddressData& ipv6Address = *v6Itr.first;

            for (std::string protocol : {"ipv4", "ipv6"})
            {
                std::string ipAddrObj =
                    "/xyz/openbmc_project/network/hypervisor/";
                ipAddrObj.append(ethIfaceId);
                ipAddrObj.append("/");
                ipAddrObj.append(protocol);
                ipAddrObj.append("/addr0");
                if (objpath.first == ipAddrObj)
                {
                    idFound = true;
                    if (interface.first == "xyz.openbmc_project.Network.IP")
                    {
                        for (const auto& property : interface.second)
                        {
                            if (property.first == "Address")
                            {
                                const std::string* address =
                                    std::get_if<std::string>(&property.second);
                                if (address == nullptr)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "extractHypervisorInterfaceData: "
                                           "Address property is not found";
                                    messages::internalError(asyncResp->res);
                                    return false;
                                }
                                if (protocol == "ipv4")
                                {
                                    ipv4Address.address = *address;
                                }
                                else if (protocol == "ipv6")
                                {
                                    ipv6Address.address = *address;
                                }
                            }
                            else if (property.first == "PrefixLength")
                            {
                                const uint8_t* mask =
                                    std::get_if<uint8_t>(&property.second);
                                if (mask == nullptr)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "extractHypervisorInterfaceData: "
                                           "PrefixLength property is not found";
                                    messages::internalError(asyncResp->res);
                                    return false;
                                }
                                if (protocol == "ipv4")
                                {
                                    ipv4Address.netmask = getNetmask(*mask);
                                }
                                else if (protocol == "ipv6")
                                {
                                    ipv6Address.prefixLength = *mask;
                                }
                            }
                            else if (property.first == "Gateway")
                            {
                                const std::string* gateway =
                                    std::get_if<std::string>(&property.second);
                                if (gateway == nullptr)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "extractHypervisorInterfaceData: "
                                           "Gateway property is not found";
                                    messages::internalError(asyncResp->res);
                                    return false;
                                }
                                if (protocol == "ipv4")
                                {
                                    ipv4Address.gateway = *gateway;
                                }
                            }
                            else
                            {
                                BMCWEB_LOG_DEBUG
                                    << "Got extra property: " << property.first
                                    << " on the " << objpath.first.str
                                    << " object";
                            }
                        }
                    }
                    else if (interface.first ==
                             "xyz.openbmc_project.Object.Enable")
                    {
                        for (const auto& property : interface.second)
                        {
                            if (property.first == "Enabled")
                            {
                                const bool* enabled =
                                    std::get_if<bool>(&property.second);
                                if (enabled == nullptr)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "extractHypervisorInterfaceData: "
                                           "Enabled property is not found";
                                    messages::internalError(asyncResp->res);
                                    return false;
                                }
                                if (protocol == "ipv4")
                                {
                                    ipv4Address.isActive = *enabled;
                                }
                                else if (protocol == "ipv6")
                                {
                                    ipv6Address.isActive = *enabled;
                                }
                            }
                        }
                    }
                }
            }
            if (objpath.first ==
                "/xyz/openbmc_project/network/hypervisor/" + ethIfaceId)
            {
                if (interface.first ==
                    "xyz.openbmc_project.Network.EthernetInterface")
                {
                    for (const auto& property : interface.second)
                    {
                        if (property.first == "DHCPEnabled")
                        {
                            const std::string* dhcpEnabled =
                                std::get_if<std::string>(&property.second);
                            if (dhcpEnabled == nullptr)
                            {
                                BMCWEB_LOG_ERROR
                                    << "extractHypervisorInterfaceData: "
                                       "DHCPEnabled property is not found";
                                messages::internalError(asyncResp->res);
                                return false;
                            }
                            ethData.dhcpEnabled = *dhcpEnabled;
                            if (!translateDhcpEnabledToBool(*dhcpEnabled, true))
                            {
                                ipv4Address.origin = "Static";
                            }
                            else
                            {
                                ipv4Address.origin = "DHCP";
                            }

                            if (!translateDhcpEnabledToBool(*dhcpEnabled,
                                                            false))
                            {
                                if (!translateSlaacEnabledToBool(*dhcpEnabled))
                                {
                                    ipv6Address.origin = "Static";
                                }
                                else
                                {
                                    ipv6Address.origin = "SLAAC";
                                }
                            }
                            else
                            {
                                ipv6Address.origin = "DHCP";
                            }
                        }
                        else if (property.first == "DefaultGateway6")
                        {
                            const std::string* defaultGateway6 =
                                std::get_if<std::string>(&property.second);
                            if (defaultGateway6 == nullptr)
                            {
                                BMCWEB_LOG_ERROR
                                    << "extractHypervisorInterfaceData: "
                                       "DefaultGateway6 property is not found";
                                messages::internalError(asyncResp->res);
                                return false;
                            }
                            std::string defaultGateway6Str = *defaultGateway6;
                            if (defaultGateway6Str.empty())
                            {
                                ethData.ipv6DefaultGateway = "::";
                            }
                            else
                            {
                                ethData.ipv6DefaultGateway = defaultGateway6Str;
                            }
                        }
                    }
                }
            }
            else if (objpath.first ==
                     "/xyz/openbmc_project/network/hypervisor/config")
            {
                if (interface.first ==
                    "xyz.openbmc_project.Network.SystemConfiguration")
                {
                    for (const auto& property : interface.second)
                    {
                        if (property.first == "HostName")
                        {
                            const std::string* hostname =
                                std::get_if<std::string>(&property.second);
                            if (hostname == nullptr)
                            {
                                BMCWEB_LOG_ERROR
                                    << "extractHypervisorInterfaceData: "
                                       "HostName property is not found";
                                messages::internalError(asyncResp->res);
                                return false;
                            }
                            ethData.hostName = *hostname;
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
 * Interface Object from Hypervisor Network Manager
 * @param ethIfaceId Hypervisor ethernet interface id to query on DBus
 * @param callback a function that shall be called to convert Dbus output
 * into JSON
 */
template <typename CallbackFunc>
void getHypervisorIfaceData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& ethIfaceId,
                            CallbackFunc&& callback)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, ethIfaceId{std::string{ethIfaceId}},
         callback{std::forward<CallbackFunc>(callback)}](
            const boost::system::error_code error,
            const dbus::utility::ManagedObjectType& resp) {
        EthernetInterfaceData ethData{};
        boost::container::flat_set<IPv4AddressData> ipv4Data;
        boost::container::flat_set<IPv6AddressData> ipv6Data;
        if (error)
        {
            callback(false, ethData, ipv4Data, ipv6Data);
            return;
        }

        bool found = extractHypervisorInterfaceData(
            asyncResp, ethIfaceId, resp, ethData, ipv4Data, ipv6Data);

        if (!found)
        {
            BMCWEB_LOG_DEBUG << "Hypervisor Interface not found";
        }
        callback(found, ethData, ipv4Data, ipv6Data);
        },
        "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
}

inline bool translateDHCPEnabledToIPv6AutoConfig(const std::string& inputDHCP)
{
    return (inputDHCP == "xyz.openbmc_project.Network.EthernetInterface."
                         "DHCPConf.v4v6stateless") ||
           (inputDHCP == "xyz.openbmc_project.Network.EthernetInterface."
                         "DHCPConf.v6stateless");
}

inline std::string
    translateIPv6AutoConfigToDHCPEnabled(const std::string& inputDHCP,
                                         const bool& ipv6AutoConfig)
{
    if (ipv6AutoConfig)
    {
        if ((inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4") ||
            (inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.both"))
        {
            return "xyz.openbmc_project.Network.EthernetInterface.DHCPConf."
                   "v4v6stateless";
        }
        if ((inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v6") ||
            (inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.none"))
        {
            return "xyz.openbmc_project.Network.EthernetInterface.DHCPConf."
                   "v6stateless";
        }
    }
    if (!ipv6AutoConfig)
    {
        if ((inputDHCP == "xyz.openbmc_project.Network.EthernetInterface."
                          "DHCPConf.v4v6stateless") ||
            (inputDHCP ==
             "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.both"))
        {
            return "xyz.openbmc_project.Network.EthernetInterface.DHCPConf.v4";
        }
        if (inputDHCP == "xyz.openbmc_project.Network.EthernetInterface."
                         "DHCPConf.v6stateless")
        {
            return "xyz.openbmc_project.Network.EthernetInterface.DHCPConf."
                   "none";
        }
    }
    return inputDHCP;
}

inline void handleHypSLAACAutoConfigPatch(
    const std::string& ifaceId, const EthernetInterfaceData& ethData,
    const bool& ipv6AutoConfigEnabled,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const std::string dhcp = translateIPv6AutoConfigToDHCPEnabled(
        ethData.dhcpEnabled, ipv6AutoConfigEnabled);
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
            messages::internalError(asyncResp->res);
            return;
        }
        messages::success(asyncResp->res);
        },
        "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor/" + ifaceId,
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Network.EthernetInterface", "DHCPEnabled",
        std::variant<std::string>{dhcp});
}

inline void
    handleHostnamePatch(const std::string& hostName,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!isHostnameValid(hostName))
    {
        messages::propertyValueFormatError(asyncResp->res, hostName,
                                           "HostName");
        return;
    }

    asyncResp->res.jsonValue["HostName"] = hostName;
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
        }
        },
        "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor/config",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Network.SystemConfiguration", "HostName",
        std::variant<std::string>(hostName));
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
inline void
    createHypervisorIP(const std::string& ifaceId, const uint8_t prefixLength,
                       const std::string& gateway, const std::string& address,
                       const std::string& protocol,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, gateway, ifaceId,
         address](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG
                << "createHypervisorIP failed: ec: " << ec.message()
                << " ec.value= " << ec.value();
            if ((ec == boost::system::errc::invalid_argument) ||
                (ec == boost::system::errc::argument_list_too_long))
            {
                messages::invalidObject(asyncResp->res,
                                        crow::utility::urlFromPieces(
                                            "redfish", "v1", "Systems",
                                            "hypervisor", "EthernetInterfaces",
                                            ifaceId));
            }
            else
            {
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

/**
 * @brief Deletes given IPv4/v6 interface
 *
 * @param[in] ifaceId     Id of interface whose IP should be deleted
 * @param[io] asyncResp   Response object that will be returned to client
 *
 * @return None
 */
inline void
    deleteHypervisorIP(const std::string& ifaceId, const std::string& protocol,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, ifaceId](const boost::system::error_code ec) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        std::string eventOrigin =
            "/redfish/v1/Systems/hypervisor/EthernetInterfaces/eth";
        eventOrigin += ifaceId.back();
        redfish::EventServiceManager::getInstance().sendEvent(
            redfish::messages::resourceChanged(), eventOrigin,
            "EthernetInterface");
        },
        "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor/" + ifaceId + "/" + protocol +
            "/addr0",
        "xyz.openbmc_project.Object.Delete", "Delete");
}

inline void parseInterfaceData(
    nlohmann::json& jsonResponse, const std::string& ifaceId,
    const EthernetInterfaceData& ethData,
    const boost::container::flat_set<IPv4AddressData>& ipv4Data,
    const boost::container::flat_set<IPv6AddressData>& ipv6Data)
{
    jsonResponse["Id"] = ifaceId;
    jsonResponse["@odata.id"] =
        "/redfish/v1/Systems/hypervisor/EthernetInterfaces/" + ifaceId;
    jsonResponse["HostName"] = ethData.hostName;
    jsonResponse["DHCPv4"]["DHCPEnabled"] =
        translateDhcpEnabledToBool(ethData.dhcpEnabled, true);
    jsonResponse["StatelessAddressAutoConfig"]["IPv6AutoConfigEnabled"] =
        translateDHCPEnabledToIPv6AutoConfig(ethData.dhcpEnabled);

    if (translateDhcpEnabledToBool(ethData.dhcpEnabled, false))
    {
        jsonResponse["DHCPv6"]["OperatingMode"] = "Enabled";
    }
    else
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
        ipv4IsActive = ipv4Config.isActive;
        nlohmann::json::object_t ipv4;
        ipv4["AddressOrigin"] = ipv4Config.origin;
        ipv4["SubnetMask"] = ipv4Config.netmask;
        ipv4["Address"] = ipv4Config.address;
        ipv4["Gateway"] = ipv4Config.gateway;
        if (ipv4Config.origin == "Static")
        {
            ipv4StaticArray.push_back(ipv4);
        }
        ipv4Array.push_back(std::move(ipv4));
    }

    std::string ipv6GatewayStr = ethData.ipv6DefaultGateway;
    if (ipv6GatewayStr.empty())
    {
        ipv6GatewayStr = "::";
    }

    jsonResponse["IPv6DefaultGateway"] = ipv6GatewayStr;

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
        ipv6IsActive = ipv6Config.isActive;
        ipv6Array.push_back({{"Address", ipv6Config.address},
                             {"PrefixLength", ipv6Config.prefixLength},
                             {"AddressOrigin", ipv6Config.origin}});
        if (ipv6Config.origin == "Static")
        {
            ipv6StaticArray.push_back(
                {{"Address", ipv6Config.address},
                 {"PrefixLength", ipv6Config.prefixLength}});
        }
    }

    if (ipv4IsActive)
    {
        jsonResponse["InterfaceEnabled"] = true;
    }
    else
    {
        if (ipv6IsActive)
        {
            jsonResponse["InterfaceEnabled"] = true;
        }
        else
        {
            jsonResponse["InterfaceEnabled"] = false;
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
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        },
        "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor/" + ifaceId,
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Network.EthernetInterface", "DHCPEnabled",
        std::variant<std::string>(ipv6DHCP));
}

inline void setDHCPEnabled(const std::string& ifaceId,
                           const EthernetInterfaceData& ethData,
                           const bool& ipv4DHCPEnabled,
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

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        },
        "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor/" + ifaceId,
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Network.EthernetInterface", "DHCPEnabled",
        std::variant<std::string>(ipv4DHCP));
}

inline void
    setIPv4InterfaceEnabled(const std::string& ifaceId, const bool& isActive,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
            messages::internalError(asyncResp->res);
            return;
        }
        },
        "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor/" + ifaceId + "/ipv4/addr0",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Object.Enable", "Enabled",
        std::variant<bool>(isActive));
}

inline void handleHypervisorIPv4StaticPatch(
    const std::string& clientIp, const std::string& ifaceId,
    const nlohmann::json& input, const EthernetInterfaceData& ethData,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if ((!input.is_array()) || input.empty())
    {
        messages::propertyValueTypeError(asyncResp->res, input.dump(),
                                         "IPv4StaticAddresses");
        return;
    }

    // Hypervisor considers the first IP address in the array list
    // as the Hypervisor's virtual management interface supports single IPv4
    // address
    const nlohmann::json& thisJson = input[0];

    // For the error string
    std::string pathString = "IPv4StaticAddresses/1";

    if (!thisJson.is_null() && !thisJson.empty())
    {
        std::optional<std::string> address;
        std::optional<std::string> subnetMask;
        std::optional<std::string> gateway;
        nlohmann::json thisJsonCopy = thisJson;
        if (!json_util::readJson(thisJsonCopy, asyncResp->res, "Address",
                                 address, "SubnetMask", subnetMask, "Gateway",
                                 gateway))
        {
            messages::propertyValueFormatError(
                asyncResp->res,
                thisJson.dump(2, ' ', true,
                              nlohmann::json::error_handler_t::replace),
                pathString);
            return;
        }

        uint8_t prefixLength = 0;
        bool errorInEntry = false;
        if (address)
        {
            if (!ipv4VerifyIpAndGetBitcount(*address))
            {
                messages::propertyValueFormatError(asyncResp->res, *address,
                                                   pathString + "/Address");
                errorInEntry = true;
            }
        }
        else
        {
            messages::propertyMissing(asyncResp->res, pathString + "/Address");
            errorInEntry = true;
        }

        if (subnetMask)
        {
            if (!ipv4VerifyIpAndGetBitcount(*subnetMask, &prefixLength))
            {
                messages::propertyValueFormatError(asyncResp->res, *subnetMask,
                                                   pathString + "/SubnetMask");
                errorInEntry = true;
            }
        }
        else
        {
            messages::propertyMissing(asyncResp->res,
                                      pathString + "/SubnetMask");
            errorInEntry = true;
        }

        if (gateway)
        {
            if (!ipv4VerifyIpAndGetBitcount(*gateway))
            {
                messages::propertyValueFormatError(asyncResp->res, *gateway,
                                                   pathString + "/Gateway");
                errorInEntry = true;
            }
        }
        else
        {
            messages::propertyMissing(asyncResp->res, pathString + "/Gateway");
            errorInEntry = true;
        }

        if (errorInEntry)
        {
            return;
        }

        BMCWEB_LOG_ERROR
            << "INFO: Static ip configuration request from client: " << clientIp
            << " - ip: " << *address << "; gateway: " << *gateway
            << "; prefix length: " << static_cast<int64_t>(prefixLength);

        createHypervisorIP(ifaceId, prefixLength, *gateway, *address,
                           "xyz.openbmc_project.Network.IP.Protocol.IPv4",
                           asyncResp);
        // Set the DHCPEnabled to false since the Static IPv4 is set
        setDHCPEnabled(ifaceId, ethData, false, asyncResp);
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
        messages::propertyValueTypeError(asyncResp->res, input.dump(),
                                         "IPv6StaticAddresses");
        return;
    }

    // Hypervisor considers the first IP address in the array list
    // as the Hypervisor's virtual management interface supports single IPv4
    // address
    const nlohmann::json& thisJson = input[0];

    // For the error string
    std::string pathString = "IPv6StaticAddresses/1";

    if (!thisJson.is_null() && !thisJson.empty())
    {
        std::optional<std::string> address;
        std::optional<uint8_t> prefixLen;
        std::optional<std::string> gateway;
        nlohmann::json thisJsonCopy = thisJson;
        if (!json_util::readJson(thisJsonCopy, asyncResp->res, "Address",
                                 address, "PrefixLength", prefixLen, "Gateway",
                                 gateway))
        {
            messages::propertyValueFormatError(
                asyncResp->res,
                thisJson.dump(2, ' ', true,
                              nlohmann::json::error_handler_t::replace),
                pathString);
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
            *gateway = "::";
        }

        if (!prefixLen)
        {
            messages::propertyMissing(asyncResp->res,
                                      pathString + "/PrefixLength");
            return;
        }

        BMCWEB_LOG_DEBUG
            << "INFO: Static ip configuration request from client: "
            << req.session->clientIp << " - ip: " << *address
            << ";gateway: " << *gateway
            << "; prefix length: " << static_cast<int64_t>(*prefixLen);

        createHypervisorIP(ifaceId, *prefixLen, *gateway, *address,
                           "xyz.openbmc_project.Network.IP.Protocol.IPv6",
                           asyncResp);
        // Set the DHCPEnabled to false since the Static IPv6 is set
        setIpv6DhcpOperatingMode(ifaceId, ethData, "Disabled", asyncResp);
    }
    else
    {
        if (thisJson.is_null())
        {
            deleteHypervisorIP(ifaceId, "ipv6", asyncResp);
        }
    }
}

inline void handleHypV6DefaultGatewayPatch(
    const std::string& ifaceId, const std::string& ipv6DefaultGateway,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
            messages::internalError(asyncResp->res);
            return;
        }
        },
        "xyz.openbmc_project.Network.Hypervisor",
        "/xyz/openbmc_project/network/hypervisor/" + ifaceId,
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Network.EthernetInterface", "DefaultGateway6",
        std::variant<std::string>(ipv6DefaultGateway));
}

inline void requestRoutesHypervisorSystems(App& app)
{
    /**
     * Hypervisor Systems derived class for delivering Computer Systems Schema.
     */

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/hypervisor/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        sdbusplus::asio::getProperty<std::string>(
            *crow::connections::systemBus,
            "xyz.openbmc_project.Network.Hypervisor",
            "/xyz/openbmc_project/network/hypervisor/config",
            "xyz.openbmc_project.Network.SystemConfiguration", "HostName",
            [asyncResp](const boost::system::error_code ec,
                        const std::string& /*hostName*/) {
            if (ec)
            {
                messages::resourceNotFound(asyncResp->res, "System",
                                           "hypervisor");
                return;
            }
            BMCWEB_LOG_DEBUG << "Hypervisor is available";

            asyncResp->res.jsonValue["@odata.type"] =
                "#ComputerSystem.v1_6_0.ComputerSystem";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/hypervisor";
            asyncResp->res.jsonValue["Description"] = "Hypervisor";
            asyncResp->res.jsonValue["Name"] = "Hypervisor";
            asyncResp->res.jsonValue["Id"] = "hypervisor";
            asyncResp->res.jsonValue["SystemType"] = "OS";
            nlohmann::json::array_t managedBy;
            nlohmann::json::object_t manager;
            manager["@odata.id"] = "/redfish/v1/Managers/bmc";
            managedBy.emplace_back(std::move(manager));
            asyncResp->res.jsonValue["Links"]["ManagedBy"] =
                std::move(managedBy);
            asyncResp->res.jsonValue["EthernetInterfaces"]["@odata.id"] =
                "/redfish/v1/Systems/hypervisor/EthernetInterfaces";
            getHypervisorState(asyncResp);
            getHypervisorActions(asyncResp);
            // TODO: Add "SystemType" : "hypervisor"
            });
        });

    /**
     * HypervisorInterfaceCollection class to handle the GET and PATCH on
     * Hypervisor Interface
     */

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/hypervisor/EthernetInterfaces/")
        .privileges(redfish::privileges::getEthernetInterfaceCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        const std::array<const char*, 1> interfaces = {
            "xyz.openbmc_project.Network.EthernetInterface"};

        crow::connections::systemBus->async_method_call(
            [asyncResp](
                const boost::system::error_code error,
                const dbus::utility::MapperGetSubTreePathsResponse& ifaceList) {
            if (error)
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
                ethIface["@odata.id"] =
                    "/redfish/v1/Systems/hypervisor/EthernetInterfaces/" + name;
                ifaceArray.push_back(std::move(ethIface));
            }
            asyncResp->res.jsonValue["Members@odata.count"] = ifaceArray.size();
            },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
            "/xyz/openbmc_project/network/hypervisor", 0, interfaces);
        });

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/hypervisor/EthernetInterfaces/<str>/")
        .privileges(redfish::privileges::getEthernetInterface)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& id) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        getHypervisorIfaceData(
            asyncResp, id,
            [asyncResp, ifaceId{std::string(id)}](
                const bool& success, const EthernetInterfaceData& ethData,
                const boost::container::flat_set<IPv4AddressData>& ipv4Data,
                const boost::container::flat_set<IPv6AddressData>& ipv6Data) {
            if (!success)
            {
                messages::resourceNotFound(asyncResp->res, "EthernetInterface",
                                           ifaceId);
                return;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#EthernetInterface.v1_8_0.EthernetInterface";
            asyncResp->res.jsonValue["Name"] = "Hypervisor Ethernet Interface";
            asyncResp->res.jsonValue["Description"] =
                "Hypervisor's Virtual Management Ethernet Interface";
            parseInterfaceData(asyncResp->res.jsonValue, ifaceId, ethData,
                               ipv4Data, ipv6Data);
            });
        });

    // Restrict the hypervisor ethernet interface PATCH to ConfigureManager
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/hypervisor/EthernetInterfaces/<str>/")
        .privileges(redfish::privileges::patchEthernetInterface)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& ifaceId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<std::string> hostName;
        std::optional<std::vector<nlohmann::json>> ipv4StaticAddresses;
        std::optional<std::vector<nlohmann::json>> ipv6StaticAddresses;
        std::optional<nlohmann::json> ipv4Addresses;
        std::optional<nlohmann::json> dhcpv4;
        std::optional<nlohmann::json> dhcpv6;
        std::optional<bool> ipv4DHCPEnabled;
        std::optional<std::string> ipv6OperatingMode;
        std::optional<nlohmann::json> statelessAddressAutoConfig;
        std::optional<bool> ipv6AutoConfigEnabled;
        std::optional<std::vector<std::string>> ipv6StaticDefaultGateways;

        if (!json_util::readJsonPatch(
                req, asyncResp->res, "HostName", hostName,
                "IPv4StaticAddresses", ipv4StaticAddresses,
                "IPv6StaticAddresses", ipv6StaticAddresses, "IPv4Addresses",
                ipv4Addresses, "DHCPv4", dhcpv4, "DHCPv6", dhcpv6,
                "StatelessAddressAutoConfig", statelessAddressAutoConfig,
                "IPv6StaticDefaultGateways", ipv6StaticDefaultGateways))
        {
            return;
        }

        if (ipv4Addresses)
        {
            messages::propertyNotWritable(asyncResp->res, "IPv4Addresses");
            return;
        }

        if (dhcpv4)
        {
            if (!json_util::readJson(*dhcpv4, asyncResp->res, "DHCPEnabled",
                                     ipv4DHCPEnabled))
            {
                return;
            }
        }

        if (dhcpv6)
        {
            if (!json_util::readJson(*dhcpv6, asyncResp->res, "OperatingMode",
                                     ipv6OperatingMode))
            {
                return;
            }
        }

        if (statelessAddressAutoConfig)
        {
            if (!json_util::readJson(*statelessAddressAutoConfig,
                                     asyncResp->res, "IPv6AutoConfigEnabled",
                                     ipv6AutoConfigEnabled))
            {
                return;
            }
        }

        const std::string& clientIp = req.session->clientIp;
        getHypervisorIfaceData(
            asyncResp, ifaceId,
            [req, clientIp, asyncResp, ifaceId, hostName = std::move(hostName),
             ipv4StaticAddresses = std::move(ipv4StaticAddresses),
             ipv6StaticAddresses = std::move(ipv6StaticAddresses),
             ipv4DHCPEnabled, dhcpv4 = std::move(dhcpv4),
             dhcpv6 = std::move(dhcpv6), ipv6OperatingMode,
             statelessAddressAutoConfig = std::move(statelessAddressAutoConfig),
             ipv6StaticDefaultGateways = std::move(ipv6StaticDefaultGateways),
             ipv6AutoConfigEnabled](
                const bool& success, const EthernetInterfaceData& ethData,
                const boost::container::flat_set<IPv4AddressData>&,
                const boost::container::flat_set<IPv6AddressData>&) {
            if (!success)
            {
                messages::resourceNotFound(asyncResp->res, "EthernetInterface",
                                           ifaceId);
                return;
            }

            if (ipv4StaticAddresses)
            {
                const nlohmann::json& ipv4Static = *ipv4StaticAddresses;
                if (ipv4Static.begin() == ipv4Static.end())
                {
                    messages::propertyValueTypeError(
                        asyncResp->res,
                        ipv4Static.dump(
                            2, ' ', true,
                            nlohmann::json::error_handler_t::replace),
                        "IPv4StaticAddresses");
                    return;
                }

                // One and only one hypervisor instance supported
                if (ipv4Static.size() != 1)
                {
                    messages::propertyValueFormatError(
                        asyncResp->res,
                        ipv4Static.dump(
                            2, ' ', true,
                            nlohmann::json::error_handler_t::replace),
                        "IPv4StaticAddresses");
                    return;
                }

                const nlohmann::json& ipv4Json = ipv4Static[0];
                // Check if the param is 'null'. If its null, it means
                // that user wants to delete the IP address. Deleting
                // the IP address is allowed only if its statically
                // configured. Deleting the address originated from DHCP
                // is not allowed.
                if ((ipv4Json.is_null()) &&
                    (translateDhcpEnabledToBool(ethData.dhcpEnabled, true)))
                {
                    BMCWEB_LOG_ERROR
                        << "Failed to delete on ipv4StaticAddresses "
                           "as the interface is DHCP enabled";
                    messages::propertyValueConflict(
                        asyncResp->res, "IPv4StaticAddresses", "DHCPEnabled");
                    return;
                }

                handleHypervisorIPv4StaticPatch(clientIp, ifaceId, ipv4Static,
                                                ethData, asyncResp);
            }

            if (ipv6StaticAddresses)
            {
                const nlohmann::json& ipv6Static = *ipv6StaticAddresses;
                if (ipv6Static.begin() == ipv6Static.end())
                {
                    messages::propertyValueTypeError(
                        asyncResp->res,
                        ipv6Static.dump(
                            2, ' ', true,
                            nlohmann::json::error_handler_t::replace),
                        "IPv6StaticAddresses");
                    return;
                }

                // One and only one hypervisor instance supported
                if (ipv6Static.size() != 1)
                {
                    messages::propertyValueFormatError(
                        asyncResp->res,
                        ipv6Static.dump(
                            2, ' ', true,
                            nlohmann::json::error_handler_t::replace),
                        "IPv6StaticAddresses");
                    return;
                }

                const nlohmann::json& ipv6Json = ipv6Static[0];
                // Check if the param is 'null'. If its null, it means
                // that user wants to delete the IP address. Deleting
                // the IP address is allowed only if its statically
                // configured. Deleting the address originated from DHCP
                // is not allowed.
                if ((ipv6Json.is_null()) &&
                    (translateDhcpEnabledToBool(ethData.dhcpEnabled, false)))
                {
                    BMCWEB_LOG_ERROR
                        << "Failed to delete on ipv6StaticAddresses "
                           "as the interface is DHCP enabled";
                    messages::propertyValueConflict(
                        asyncResp->res, "IPv6StaticAddresses", "DHCPEnabled");
                    return;
                }
                handleHypervisorIPv6StaticPatch(req, ifaceId, ipv6Static,
                                                ethData, asyncResp);
            }

            if (hostName)
            {
                handleHostnamePatch(*hostName, asyncResp);
            }

            if (dhcpv4)
            {
                setDHCPEnabled(ifaceId, ethData, *ipv4DHCPEnabled, asyncResp);
            }

            if (dhcpv6)
            {
                setIpv6DhcpOperatingMode(ifaceId, ethData, *ipv6OperatingMode,
                                         asyncResp);
            }

            if (statelessAddressAutoConfig)
            {
                handleHypSLAACAutoConfigPatch(
                    ifaceId, ethData, *ipv6AutoConfigEnabled, asyncResp);
            }

            if (ipv6StaticDefaultGateways)
            {
                const std::vector<std::string>& ipv6StaticDefaultGw =
                    *ipv6StaticDefaultGateways;
                if ((ipv6StaticDefaultGw).size() > 1)
                {
                    messages::propertyValueModified(
                        asyncResp->res, "IPv6StaticDefaultGateways",
                        ipv6StaticDefaultGw.front());
                }
                handleHypV6DefaultGatewayPatch(
                    ifaceId, ipv6StaticDefaultGw.front(), asyncResp);
            }
            });
        asyncResp->res.result(boost::beast::http::status::accepted);
        });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/hypervisor/ResetActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        // Only return action info if hypervisor D-Bus object present
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& objInfo) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error " << ec;

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
            parameter["DataType"] = "String";
            nlohmann::json::array_t allowed;
            allowed.emplace_back("On");
            parameter["AllowableValues"] = std::move(allowed);
            parameters.emplace_back(std::move(parameter));
            asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
            },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject",
            "/xyz/openbmc_project/state/hypervisor0",
            std::array<const char*, 1>{"xyz.openbmc_project.State.Host"});
        });

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/hypervisor/Actions/ComputerSystem.Reset/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<std::string> resetType;
        if (!json_util::readJsonAction(req, asyncResp->res, "ResetType",
                                       resetType))
        {
            // readJson adds appropriate error to response
            return;
        }

        if (!resetType)
        {
            messages::actionParameterMissing(
                asyncResp->res, "ComputerSystem.Reset", "ResetType");
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

        crow::connections::systemBus->async_method_call(
            [asyncResp, resetType](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
                if (ec.value() == boost::asio::error::invalid_argument)
                {
                    messages::actionParameterNotSupported(asyncResp->res,
                                                          *resetType, "Reset");
                    return;
                }

                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(asyncResp->res, "Actions",
                                               "Reset");
                    return;
                }

                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
            },
            "xyz.openbmc_project.State.Hypervisor",
            "/xyz/openbmc_project/state/hypervisor0",
            "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.State.Host", "RequestedHostTransition",
            dbus::utility::DbusVariantType{std::move(command)});
        });
}
} // namespace redfish::hypervisor
