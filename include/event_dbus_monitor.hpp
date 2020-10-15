#include <dbus_utility.hpp>
#include <error_messages.hpp>
#include <event_service_manager.hpp>
#include <resource_messages.hpp>

namespace crow
{
namespace dbus_monitor
{

static std::shared_ptr<sdbusplus::bus::match::match> matchHostStateChange;
static std::shared_ptr<sdbusplus::bus::match::match> matchBMCStateChange;
static std::shared_ptr<sdbusplus::bus::match::match> matchVMIIPStateChange;

void registerHostStateChangeSignal();
void registerBMCStateChangeSignal();
void registerVMIIPChangeSignal();

inline void VMIIPChange(sdbusplus::message::message& msg)
{
    BMCWEB_LOG_DEBUG << "BMC Hypervisor IP change match fired";

    if (msg.is_method_error())
    {
        BMCWEB_LOG_ERROR << "BMC Hypervisor IP property changed Signal error";
        return;
    }

    BMCWEB_LOG_DEBUG << msg.get_path();

    std::string infname;
    dbus::utility::getNthStringFromPath(msg.get_path(), 4, infname);
    BMCWEB_LOG_DEBUG << infname;

    boost::container::flat_map<std::string,
                               std::variant<std::string, uint8_t, bool>>
        values;
    std::string objName;
    msg.read(objName, values);
    BMCWEB_LOG_DEBUG << objName;

    auto find = values.find("Enabled");
    if (find == values.end())
    {
        BMCWEB_LOG_ERROR << "Enabled property not Found";
        return;
    }
    BMCWEB_LOG_DEBUG << "Enabled property found";
    BMCWEB_LOG_DEBUG << find->first;

    const bool* propValue = std::get_if<bool>(&(find->second));
    if (propValue != nullptr)
    {
        BMCWEB_LOG_DEBUG << *propValue;

        if (*propValue)
        {
            // pldm recieves a sensor event from the hypervisor when the vmi is
            // completely configured with an IP Address, post reciveing the
            // sensor event, pldm will sent the Enable property to True.
            //
            // HMC is only interested in false -> true transition of Enabled
            // property as that is when vmi is configured.
            //
            // Push an event
            std::string origin =
                "/redfish/v1/Systems/hypervisor/EthernetInterfaces/" + infname;
            BMCWEB_LOG_DEBUG << "Pushing the VMI IP change Event with origin : "
                             << origin;
            redfish::EventServiceManager::getInstance().sendEvent(
                redfish::messages::resourceChanged(), origin,
                "EthernetInterface");
        }
    }
}

inline void BMCStatePropertyChange(sdbusplus::message::message& msg)
{
    BMCWEB_LOG_DEBUG << "BMC state change match fired";

    if (msg.is_method_error())
    {
        BMCWEB_LOG_ERROR << "BMC state property changed Signal error";
        return;
    }
    std::string iface;
    boost::container::flat_map<std::string, std::variant<std::string, uint8_t>>
        values;
    std::string objName;
    msg.read(objName, values);
    auto find = values.find("CurrentBMCState");
    if (find == values.end())
    {
        registerBMCStateChangeSignal();
        return;
    }
    std::string* type = std::get_if<std::string>(&(find->second));
    if (type != nullptr)
    {
        BMCWEB_LOG_DEBUG << *type;
        // Push an event
        std::string origin = "/redfish/v1/Managers/bmc";
        redfish::EventServiceManager::getInstance().sendEvent(
            redfish::messages::resourceChanged(), origin, "Manager");
    }
    registerBMCStateChangeSignal();
}

inline void HostStatePropertyChange(sdbusplus::message::message& msg)
{
    BMCWEB_LOG_DEBUG << "Host state change match fired";

    if (msg.is_method_error())
    {
        BMCWEB_LOG_ERROR << "Host state property changed Signal error";
        return;
    }
    std::string iface;
    boost::container::flat_map<std::string, std::variant<std::string, uint8_t>>
        values;
    std::string objName;
    msg.read(objName, values);
    auto find = values.find("CurrentHostState");
    if (find == values.end())
    {
        registerHostStateChangeSignal();
        return;
    }
    std::string* type = std::get_if<std::string>(&(find->second));
    if (type != nullptr)
    {
        BMCWEB_LOG_DEBUG << *type;
        // Push an event
        std::string origin = "/redfish/v1/Systems/system";
        redfish::EventServiceManager::getInstance().sendEvent(
            redfish::messages::resourceChanged(), origin, "ComputerSystem");
    }
    registerHostStateChangeSignal();
}

void registerHostStateChangeSignal()
{
    BMCWEB_LOG_DEBUG << "Host state change signal - Register";

    matchHostStateChange = std::make_unique<sdbusplus::bus::match::match>(
        *crow::connections::systemBus,
        "type='signal',member='PropertiesChanged',interface='org.freedesktop."
        "DBus.Properties',path='/xyz/openbmc_project/state/host0',"
        "arg0='xyz.openbmc_project.State.Host'",
        HostStatePropertyChange);
}

void registerBMCStateChangeSignal()
{

    BMCWEB_LOG_DEBUG << "BMC state change signal - Register";

    matchBMCStateChange = std::make_unique<sdbusplus::bus::match::match>(
        *crow::connections::systemBus,
        "type='signal',member='PropertiesChanged',interface='org.freedesktop."
        "DBus.Properties',path='/xyz/openbmc_project/state/bmc0',"
        "arg0='xyz.openbmc_project.State.BMC'",
        BMCStatePropertyChange);
}

void registerVMIIPChangeSignal()
{

    BMCWEB_LOG_DEBUG << "VMI IP change signal match - Registered";

    matchVMIIPStateChange = std::make_unique<sdbusplus::bus::match::match>(
        *crow::connections::systemBus,
        "type='signal',member='PropertiesChanged',interface='org.freedesktop."
        "DBus.Properties',arg0namespace='xyz.openbmc_project.Object.Enable'",
        VMIIPChange);
}

void registerStateChangeSignal()
{
    registerHostStateChangeSignal();
    registerBMCStateChangeSignal();
    registerVMIIPChangeSignal();
}

} // namespace dbus_monitor
} // namespace crow
