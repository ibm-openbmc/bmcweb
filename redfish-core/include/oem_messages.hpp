#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace redfish
{

namespace messages
{
// Additional OEM taskAborted
nlohmann::json taskAborted(const std::string& arg1, const std::string& arg2,
                           const std::string& arg3, const std::string& arg4);
} // namespace messages
} // namespace redfish
