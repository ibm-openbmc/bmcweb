/****************************************************************
 *                 READ THIS WARNING FIRST
 * This is an auto-generated header which contains definitions
 * for Redfish DMTF defined messages.
 * DO NOT modify this registry outside of running the
 * parse_registries.py script.  The definitions contained within
 * this file are owned by DMTF.  Any modifications to these files
 * should be first pushed to the relevant registry in the DMTF
 * github organization.
 ***************************************************************/
#include "license_messages.hpp"

#include "registries.hpp"
#include "registries/license_event_message_registry.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
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

static nlohmann::json getLog(redfish::registries::license_event::Index name,
                             std::span<const std::string_view> args)
{
    size_t index = static_cast<size_t>(name);
    if (index >= redfish::registries::license_event::registry.size())
    {
        return {};
    }
    return getLogFromRegistry(redfish::registries::license_event::header,
                              redfish::registries::license_event::registry,
                              index, args);
}

/**
 * @internal
 * @brief Formats LicenseInstalled message into JSON
 *
 * See header file for more information
 * @endinternal
 */
nlohmann::json licenseInstalled(std::string_view arg1)
{
    return getLog(redfish::registries::license_event::Index::licenseInstalled,
                  std::to_array({arg1}));
}

/**
 * @internal
 * @brief Formats DaysBeforeExpiration message into JSON
 *
 * See header file for more information
 * @endinternal
 */
nlohmann::json daysBeforeExpiration(std::string_view arg1,
                                    std::string_view arg2)
{
    return getLog(
        redfish::registries::license_event::Index::daysBeforeExpiration,
        std::to_array({arg1, arg2}));
}

/**
 * @internal
 * @brief Formats GracePeriod message into JSON
 *
 * See header file for more information
 * @endinternal
 */
nlohmann::json gracePeriod(std::string_view arg1, std::string_view arg2)
{
    return getLog(redfish::registries::license_event::Index::gracePeriod,
                  std::to_array({arg1, arg2}));
}

/**
 * @internal
 * @brief Formats Expired message into JSON
 *
 * See header file for more information
 * @endinternal
 */
nlohmann::json expired(std::string_view arg1)
{
    return getLog(redfish::registries::license_event::Index::expired,
                  std::to_array({arg1}));
}

/**
 * @internal
 * @brief Formats InstallFailed message into JSON
 *
 * See header file for more information
 * @endinternal
 */
nlohmann::json installFailed(std::string_view arg1)
{
    return getLog(redfish::registries::license_event::Index::installFailed,
                  std::to_array({arg1}));
}

/**
 * @internal
 * @brief Formats InvalidLicense message into JSON
 *
 * See header file for more information
 * @endinternal
 */
nlohmann::json invalidLicense()
{
    return getLog(redfish::registries::license_event::Index::invalidLicense,
                  {});
}

/**
 * @internal
 * @brief Formats NotApplicableToTarget message into JSON
 *
 * See header file for more information
 * @endinternal
 */
nlohmann::json notApplicableToTarget()
{
    return getLog(
        redfish::registries::license_event::Index::notApplicableToTarget, {});
}

/**
 * @internal
 * @brief Formats TargetsRequired message into JSON
 *
 * See header file for more information
 * @endinternal
 */
nlohmann::json targetsRequired()
{
    return getLog(redfish::registries::license_event::Index::targetsRequired,
                  {});
}

} // namespace messages
} // namespace redfish
