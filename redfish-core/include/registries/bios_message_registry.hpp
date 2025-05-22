#pragma once

#include "registries.hpp"

namespace redfish::registries::bios
{
const Header header = {
    "Copyright 2020 OpenBMC. All rights reserved.",
    "#MessageRegistry.v1_4_0.MessageRegistry",
    1,
    0,
    0,
    "Bios Attribute Registry",
    "en",
    "This registry defines the messages for bios attribute registry.",
    "BiosAttributeRegistry",
    "DMTF",
};
constexpr const char* url = nullptr;
} // namespace redfish::registries::bios
