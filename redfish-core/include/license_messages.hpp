
#include "http_response.hpp"
#include <nlohmann/json.hpp>

namespace redfish
{

namespace messages
{

static void addMessageToErrorJson(nlohmann::json& target,
                                  const nlohmann::json& message)
{
    auto& error = target["error"];

    // If this is the first error message, fill in the information from the
    // first error message to the top level struct
    if (!error.is_object())
    {
        auto messageIdIterator = message.find("MessageId");
        if (messageIdIterator == message.end())
        {
            BMCWEB_LOG_CRITICAL
                << "Attempt to add error message without MessageId";
            return;
        }

        auto messageFieldIterator = message.find("Message");
        if (messageFieldIterator == message.end())
        {
            BMCWEB_LOG_CRITICAL
                << "Attempt to add error message without Message";
            return;
        }
        error = {{"code", *messageIdIterator},
                 {"message", *messageFieldIterator}};
    }
    else
    {
        // More than 1 error occurred, so the message has to be generic
        error["code"] = std::string(messageVersionPrefix) + "GeneralError";
        error["message"] = "A general error has occurred. See Resolution for "
                           "information on how to resolve the error.";
    }

    // This check could technically be done in in the default construction
    // branch above, but because we need the pointer to the extended info field
    // anyway, it's more efficient to do it here.
    auto& extendedInfo = error[messages::messageAnnotation];
    if (!extendedInfo.is_array())
    {
        extendedInfo = nlohmann::json::array();
    }

    extendedInfo.push_back(message);
}

inline nlohmann::json licenseInstalled(const std::string& arg1)
{
    return nlohmann::json{{"@odata.type", "#Message.v1_0_0.Message"},
                          {"MessageId", "License.1.0.0.LicenseInstalled"},
                          {"Message", "The license has been installed."},
                          {"MessageArgs", {arg1}},
                          {"Severity", "OK"},
                          {"Resolution", "None."}};
}

void licenseInstalled(crow::Response& res)
{
    res.result(boost::beast::http::status::internal_server_error);
    addMessageToErrorJson(res.jsonValue,
                          licenseInstalled("Host license installed"));
}

inline nlohmann::json invalidLicense()
{
    return nlohmann::json{
        {"@odata.type", "#Message.v1_0_0.Message"},
        {"MessageId", "License.1.0.0.InvalidLicense"},
        {"Message", "The content of the license was not recognized, is "
                    "corrupted, or is invalid."},
        {"MessageArgs", {}},
        {"Severity", "Critical"},
        {"Resolution", "None."}};
}

void invalidLicense(crow::Response& res)
{
    res.result(boost::beast::http::status::bad_request);
    addMessageToErrorJson(res.jsonValue, invalidLicense());
}

inline nlohmann::json installFailed(const std::string& arg1)
{
    return nlohmann::json{
        {"@odata.type", "#Message.v1_0_0.Message"},
        {"MessageId", "License.1.0.0.InstallFailed"},
        {"Message", "Failed to install the license.  Reason: %1."},
        {"MessageArgs", {arg1}},
        {"Severity", "Critical"},
        {"Resolution", "None."}};
}

void installFailed(crow::Response& res)
{
    res.result(boost::beast::http::status::internal_server_error);
    addMessageToErrorJson(res.jsonValue,
                          installFailed("Server failed to install"));
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

void notApplicableToTarget(crow::Response& res)
{
    res.result(boost::beast::http::status::internal_server_error);
    addMessageToErrorJson(res.jsonValue, notApplicableToTarget());
}

} // namespace messages
} // namespace redfish
