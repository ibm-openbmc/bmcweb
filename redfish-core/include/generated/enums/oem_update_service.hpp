// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace oem_update_service
{
// clang-format off

enum class BootSide{
    Invalid,
    Temp,
    Perm,
    Unknown,
};

NLOHMANN_JSON_SERIALIZE_ENUM(BootSide, {
    {BootSide::Invalid, "Invalid"},
    {BootSide::Temp, "Temp"},
    {BootSide::Perm, "Perm"},
    {BootSide::Unknown, "Unknown"},
});

// clang-format on
} // namespace oem_update_service
