
#include "http_response.hpp"

#include <error_messages.hpp>
#include <nlohmann/json.hpp>

namespace redfish
{

namespace messages
{

inline nlohmann::json licenseInstalled(const std::string& arg1)
{
    return nlohmann::json{{"@odata.type", "#Message.v1_0_0.Message"},
                          {"MessageId", "License.1.0.0.LicenseInstalled"},
                          {"Message", "The license has been installed " + arg1},
                          {"MessageArgs", {arg1}},
                          {"Severity", "OK"},
                          {"Resolution", "None."}};
}

static void licenseInstalled(crow::Response& res,
                             const std::string& licenseString)
{
    res.result(boost::beast::http::status::ok);
    addMessageToJson(res.jsonValue, licenseInstalled(licenseString),
                     "LicenseString");
}

inline nlohmann::json invalidLicense()
{
    return nlohmann::json{{"@odata.type", "#Message.v1_0_0.Message"},
                          {"MessageId", "License.1.0.0.InvalidLicense"},
                          {"Message",
                           "The content of the license was not recognized, is "
                           "corrupted, or is invalid."},
                          {"MessageArgs", {}},
                          {"Severity", "Critical"},
                          {"Resolution", "None."}};
}

static void invalidLicense(crow::Response& res)
{
    res.result(boost::beast::http::status::bad_request);
    addMessageToErrorJson(res.jsonValue, invalidLicense());
}

inline nlohmann::json installFailed(const std::string& arg1)
{
    return nlohmann::json{
        {"@odata.type", "#Message.v1_0_0.Message"},
        {"MessageId", "License.1.0.0.InstallFailed"},
        {"Message", "Failed to install the license.  Reason: " + arg1},
        {"MessageArgs", {arg1}},
        {"Severity", "Critical"},
        {"Resolution", "None."}};
}

static void installFailed(crow::Response& res, const std::string& reason)
{
    res.result(boost::beast::http::status::internal_server_error);
    addMessageToErrorJson(res.jsonValue, installFailed(reason));
}

inline nlohmann::json notApplicableToTarget()
{
    return nlohmann::json{
        {"@odata.type", "#Message.v1_0_0.Message"},
        {"MessageId", "License.1.0.0.notApplicableToTarget"},
        {"Message",
         "Indicates that the license is not applicable to the target."},
        {"MessageArgs", {}},
        {"Severity", "Critical"},
        {"Resolution", "None."}};
}

static void notApplicableToTarget(crow::Response& res)
{
    res.result(boost::beast::http::status::bad_request);
    addMessageToErrorJson(res.jsonValue, notApplicableToTarget());
}

} // namespace messages
} // namespace redfish
