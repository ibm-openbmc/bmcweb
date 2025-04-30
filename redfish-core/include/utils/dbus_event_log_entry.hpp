#pragma once

#include "dbus_utility.hpp"
#include "utils/dbus_utils.hpp"

#include <sdbusplus/unpack_properties.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace redfish
{
struct DbusEventLogEntry
{
    // represents a subset of an instance of dbus interface
    // xyz.openbmc_project.Logging.Entry

    uint32_t Id = 0;
    std::string Message;
    const std::string* Path = nullptr;
    const std::string* Resolution = nullptr;
    bool Resolved = false;
    std::string ServiceProviderNotify;
    std::string Severity;
    uint64_t Timestamp = 0;
    uint64_t UpdateTimestamp = 0;
    // Additional elements from upstream
    std::string EventId;
    bool Hidden = false;
    bool ManagementSystemAck = false;
    std::string Subsystem;
};

inline std::optional<DbusEventLogEntry> fillDbusEventLogEntryFromPropertyMap(
    const dbus::utility::DBusPropertiesMap& resp)
{
    DbusEventLogEntry entry;

    bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), resp,               //
        "Id", entry.Id,                                       //
        "EventId", entry.EventId,                             //
        "Message", entry.Message,                             //
        "Path", entry.Path,                                   //
        "Resolution", entry.Resolution,                       //
        "Resolved", entry.Resolved,                           //
        "ServiceProviderNotify", entry.ServiceProviderNotify, //
        "Severity", entry.Severity,                           //
        "Timestamp", entry.Timestamp,                         //
        "UpdateTimestamp", entry.UpdateTimestamp,             //
        "Hidden", entry.Hidden,                               //
        "ManagementSystemAck", entry.ManagementSystemAck,     //
        "Subsystem", entry.Subsystem                          //
    );
    if (!success)
    {
        return std::nullopt;
    }
    return entry;
}
} // namespace redfish
