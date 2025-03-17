
#include "oem_messages.hpp"

#include "bmcweb_config.h"

#include "registries.hpp"
#include "registries/task_event_message_registry.hpp"

#include <nlohmann/json.hpp>

#include <format>
#include <string>
#include <string_view>

// Clang can't seem to decide whether this header needs to be included or not,
// and is inconsistent.  Include it for now
// NOLINTNEXTLINE(misc-include-cleaner)
#include <utility>

namespace redfish
{

namespace messages
{

/**
 * @internal
 * @brief Formats TaskAborted message into JSON
 *
 * See header file for more information
 * @endinternal
 */

// Additional OEM taskAborted
nlohmann::json taskAborted(const std::string& arg1, const std::string& arg2,
                           const std::string& arg3, const std::string& arg4)
{
    const redfish::registries::Header& header =
        redfish::registries::task_event::header;

    std::string msgId;
    std::string_view msgName = "TaskAborted";
    if constexpr (BMCWEB_REDFISH_USE_3_DIGIT_MESSAGEID)
    {
        msgId = std::format("{}.{}.{}.{}.{}", header.registryPrefix,
                            header.versionMajor, header.versionMinor,
                            header.versionPatch, msgName);
    }
    else
    {
        msgId = std::format("{}.{}.{}.{}", header.registryPrefix,
                            header.versionMajor, header.versionMinor, msgName);
    }
    return nlohmann::json{
        {"@odata.type", "#Message.v1_0_0.Message"},
        {"MessageId", msgId},
        {"Message", "The task with id " + arg1 + " has been aborted."},
        {"MessageArgs", {arg1, arg2, arg3, arg4}},
        {"Severity", "Critical"},
        {"Resolution", "None."},
        {"Oem",
         {{"OpenBMC",
           {{"@odata.type", "#OpenBMCMessage.v1_0_0.Message"},
            {"AbortReason", arg2},
            {"AdditionalData", arg3},
            {"EventId", arg4}}}}}};
}

} // namespace messages
} // namespace redfish
