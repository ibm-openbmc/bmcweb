
/****************************************************************
 * This is an auto-generated header which contains definitions
 * for Redfish DMTF defined messages.
 ***************************************************************/
#pragma once
#include <registries.hpp>

#include <array>

namespace redfish::registries::license
{
const Header header = {
    "Copyright 2014-2021 DMTF. All rights reserved.",
    "#MessageRegistry.v1_4_2.MessageRegistry",
    "License.1.0.0",
    "License Message Registry",
    "en",
    "This registry defines the license status and error messages.",
    "License",
    "1.0.0",
    "DMTF",
};
constexpr const char* url = "https://redfish.dmtf.org/registries/License.1.0.0";

constexpr std::array registry = {
    MessageEntry{"DaysBeforeExpiration",
                 {
                     "Indicates the number of days remaining on a license "
                     "before expiration.",
                     "The license '%1' will expire in %2 days.",
                     "OK",
                     2,
                     {
                         "string",
                         "number",
                     },
                     "None.",
                 }},
    MessageEntry{"Expired",
                 {
                     "Indicates that a license has expired and its "
                     "functionality has been disabled.",
                     "The license '%1' has expired.",
                     "Warning",
                     1,
                     {
                         "string",
                     },
                     "None.",
                 }},
    MessageEntry{"GracePeriod",
                 {
                     "Indicates that a license has expired and entered its "
                     "grace period.",
                     "The license '%1' has expired, %2 day grace period before "
                     "licensed functionality is disabled.",
                     "Warning",
                     2,
                     {
                         "string",
                         "number",
                     },
                     "None.",
                 }},
    MessageEntry{
        "InstallFailed",
        {
            "Indicates that the service failed to install the license.",
            "Failed to install the license.  Reason: %1.",
            "Critical",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "InvalidLicense",
        {
            "Indicates that the license was not recognized, is corrupted, or "
            "is invalid.",
            "The content of the license was not recognized, is corrupted, or "
            "is invalid.",
            "Critical",
            0,
            {},
            "Verify the license content is correct and resubmit the request.",
        }},
    MessageEntry{"LicenseInstalled",
                 {
                     "Indicates that a license has been installed.",
                     "The license '%1' has been installed.",
                     "OK",
                     1,
                     {
                         "string",
                     },
                     "None.",
                 }},
    MessageEntry{
        "NotApplicableToTarget",
        {
            "Indicates that the license is not applicable to the target.",
            "The license is not applicable to the target.",
            "Critical",
            0,
            {},
            "Check the license compatibility or applicability to the specified "
            "target.",
        }},
    MessageEntry{
        "TargetsRequired",
        {
            "Indicates that one or more targets need to be specified with the "
            "license.",
            "The license requires targets to be specified.",
            "Critical",
            0,
            {},
            "Add `AuthorizedDevices` to `Links` and resubmit the request.",
        }},
};
} // namespace redfish::registries::license
