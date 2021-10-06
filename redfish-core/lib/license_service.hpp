#pragma once

#include "registries.hpp"

#include <app.hpp>
#include <error_messages.hpp>
#include <registries/privilege_registry.hpp>

#include <filesystem>
#include <string_view>
#include <variant>

namespace redfish
{

using GetManagedObjectsType = boost::container::flat_map<
    sdbusplus::message::object_path,
    boost::container::flat_map<std::string, GetManagedPropertyType>>;

static std::unique_ptr<sdbusplus::bus::match::match>
    licenseActivationStatusMatch;

inline void getLicenseEntryCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    GetManagedObjectsType& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "LicenseEntry resp_handler got error "
                                 << ec;
                // continue here to show zero members
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }

            nlohmann::json& entriesArray = asyncResp->res.jsonValue["Members"];
            entriesArray = nlohmann::json::array();

            for (const auto& object : resp)
            {
                std::string entryID = object.first.filename();
                if (entryID.empty())
                {
                    continue;
                }
                entriesArray.push_back({});
                nlohmann::json& thisEntry = entriesArray.back();
                thisEntry["@odata.id"] =
                    "/redfish/v1/LicenseService/Licenses/" + entryID;
                thisEntry["Id"] = entryID;
                thisEntry["Name"] = entryID + " License Entry";
                asyncResp->res.jsonValue["Members@odata.count"] =
                    entriesArray.size();
            }
        },
        "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/license",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
}

inline void requestRoutesLicenseService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/LicenseService/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/LicenseService/";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LicenseService.v1_0_0.LicenseService";
                asyncResp->res.jsonValue["Name"] = "License Service";
                asyncResp->res.jsonValue["Id"] = "LicenseService";

                asyncResp->res.jsonValue["Licenses"] = {
                    {"@odata.id", "/redfish/v1/LicenseService/Licenses"}};
                asyncResp->res.jsonValue["Actions"] = {
                    {"#LicenseService.Install",
                     {{
                         "target",
                         "/redfish/v1/LicenseService/Actions/"
                         "LicenseService.Install",
                     }}}};
            });
}

inline void
    getLicenseActivationAck(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& status)
{

    if (status == "com.ibm.License.LicenseManager.Status.ActivationFailed")
    {
        // TODO: Need to return appropriate redfish error
        BMCWEB_LOG_ERROR << "LicenseActivationStatus: ActivationFailed";
        messages::internalError(asyncResp->res);
    }
    else if (status == "com.ibm.License.LicenseManager.Status.InvalidLicense")
    {
        // TODO: Need to return appropriate redfish error
        BMCWEB_LOG_ERROR << "LicenseActivationStatus: InvalidLicense";
        messages::internalError(asyncResp->res);
    }
    else if (status == "com.ibm.License.LicenseManager.Status.IncorrectSystem")
    {
        // TODO: Need to return appropriate redfish error
        BMCWEB_LOG_ERROR << "LicenseActivationStatus: IncorrectSystem";
        messages::internalError(asyncResp->res);
    }
    else if (status ==
             "com.ibm.License.LicenseManager.Status.IncorrectSequence")
    {
        // TODO: Need to return appropriate redfish error
        BMCWEB_LOG_ERROR << "LicenseActivationStatus: IncorrectSequence";
        messages::internalError(asyncResp->res);
    }
    else if (status == "com.ibm.License.LicenseManager.Status.InvalidHostState")
    {
        // TODO: Need to return appropriate redfish error
        BMCWEB_LOG_ERROR << "LicenseActivationStatus: InvalidHostState";
        messages::internalError(asyncResp->res);
    }
    else if (status == "com.ibm.License.LicenseManager.Status.Activated")
    {
        BMCWEB_LOG_INFO << "License Activated";
    }
    else
    {
        messages::internalError(asyncResp->res);
    }
    licenseActivationStatusMatch = nullptr;
}

inline void requestRoutesLicenseEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/LicenseService/Licenses")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LicenseCollection.LicenseCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/LicenseService/Licenses";
                asyncResp->res.jsonValue["Name"] = "License Collection";

                getLicenseEntryCollection(asyncResp);
            });

    BMCWEB_ROUTE(app, "/redfish/v1/LicenseService/Licenses")
        .privileges({{"ConfigureManager"}})
        .methods(
            boost::beast::http::verb::
                post)([](const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            std::string licenseString;
            if (!redfish::json_util::readJson(req, asyncResp->res,
                                              "LicenseString", licenseString))
            {
                return;
            }

            if (licenseString.empty())
            {
                messages::propertyMissing(asyncResp->res, "LicenseString");
            }

            // Only allow one License install at a time
            if (licenseActivationStatusMatch != nullptr)
            {
                messages::resourceInUse(asyncResp->res);
                return;
            }

            std::shared_ptr<boost::asio::steady_timer> timeout =
                std::make_shared<boost::asio::steady_timer>(
                    crow::connections::systemBus->get_io_context());
            timeout->expires_after(std::chrono::seconds(10));
            crow::connections::systemBus->async_method_call(
                [timeout, asyncResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR
                            << "LicenseString resp_handler got error " << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    auto timeoutHandler =
                        [asyncResp,
                         timeout](const boost::system::error_code ec) {
                            licenseActivationStatusMatch = nullptr;
                            if (ec)
                            {
                                if (ec != boost::asio::error::operation_aborted)
                                {
                                    BMCWEB_LOG_ERROR << "Async_wait failed "
                                                     << ec;
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                            }
                            else
                            {
                                messages::serviceTemporarilyUnavailable(
                                    asyncResp->res, "60");
                                BMCWEB_LOG_ERROR
                                    << "Timed out waiting for HostInterface to "
                                       "serve license upload request";
                                return;
                            }
                        };

                    timeout->async_wait(timeoutHandler);

                    auto callback = [asyncResp,
                                     timeout](sdbusplus::message::message& m) {
                        BMCWEB_LOG_DEBUG << "Response Matched " << m.get();
                        boost::container::flat_map<std::string,
                                                   std::variant<std::string>>
                            values;
                        std::string iface;
                        m.read(iface, values);
                        if (iface == "com.ibm.License.LicenseManager")
                        {
                            auto findStatus =
                                values.find("LicenseActivationStatus");
                            if (findStatus != values.end())
                            {
                                BMCWEB_LOG_INFO
                                    << "Found Status property change";
                                std::string* status = std::get_if<std::string>(
                                    &(findStatus->second));
                                if (status == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                getLicenseActivationAck(asyncResp, *status);
                                timeout->cancel();
                            }
                        }
                    };

                    licenseActivationStatusMatch = std::make_unique<
                        sdbusplus::bus::match::match>(
                        *crow::connections::systemBus,
                        "interface='org.freedesktop.DBus.Properties',type='"
                        "signal',"
                        "member='PropertiesChanged',path='/com/ibm/license'",
                        callback);
                },
                "com.ibm.License.Manager", "/com/ibm/license",
                "org.freedesktop.DBus.Properties", "Set",
                "com.ibm.License.LicenseManager", "LicenseString",
                std::variant<std::string>(licenseString));
        });
}

inline void
    getLicenseEntryById(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& licenseEntryID)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, licenseEntryID](const boost::system::error_code ec,
                                    GetManagedObjectsType& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "LicenseEntry resp_handler got error "
                                 << ec;
                messages::internalError(asyncResp->res);
                return;
            }

            std::string licenseEntryPath =
                "/xyz/openbmc_project/license/entry/" + licenseEntryID;

            for (const auto& objectPath : resp)
            {
                if (objectPath.first.str != licenseEntryPath)
                {
                    continue;
                }

                std::time_t expirationTime;
                const uint32_t* deviceNumPtr = nullptr;
                const std::string* serialNumPtr = nullptr;
                const std::string* licenseNamePtr = nullptr;
                const std::string* licenseTypePtr = nullptr;
                const std::string* authorizationTypePtr = nullptr;
                bool available = false;
                bool state = false;

                for (const auto& interfaceMap : objectPath.second)
                {
                    if (interfaceMap.first ==
                        "com.ibm.License.Entry.LicenseEntry")
                    {
                        for (const auto& propertyMap : interfaceMap.second)
                        {
                            if (propertyMap.first == "Name")
                            {
                                licenseNamePtr = std::get_if<std::string>(
                                    &propertyMap.second);
                                if (licenseNamePtr == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    break;
                                }
                            }
                            else if (propertyMap.first == "Type")
                            {
                                licenseTypePtr = std::get_if<std::string>(
                                    &propertyMap.second);
                                if (licenseTypePtr == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    break;
                                }
                            }
                            else if (propertyMap.first == "AuthorizationType")
                            {
                                authorizationTypePtr = std::get_if<std::string>(
                                    &propertyMap.second);
                                if (authorizationTypePtr == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    break;
                                }
                            }
                            else if (propertyMap.first == "AuthDeviceNumber")
                            {
                                deviceNumPtr =
                                    std::get_if<uint32_t>(&propertyMap.second);
                                if (deviceNumPtr == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    break;
                                }
                            }
                            else if (propertyMap.first == "ExpirationTime")
                            {
                                const uint64_t* timePtr =
                                    std::get_if<uint64_t>(&propertyMap.second);
                                if (timePtr == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    break;
                                }
                                expirationTime =
                                    static_cast<std::time_t>(*timePtr);
                            }
                            else if (propertyMap.first == "SerialNumber")
                            {
                                serialNumPtr = std::get_if<std::string>(
                                    &propertyMap.second);
                                if (serialNumPtr == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    break;
                                }
                            }
                        }
                    }
                    else if (interfaceMap.first ==
                             "xyz.openbmc_project.State.Decorator.Availability")
                    {
                        for (const auto& propertyMap : interfaceMap.second)
                        {
                            if (propertyMap.first == "Available")
                            {
                                const bool* availablePtr =
                                    std::get_if<bool>(&propertyMap.second);
                                if (availablePtr == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    break;
                                }
                                available = *availablePtr;
                            }
                        }
                    }
                    if (interfaceMap.first ==
                        "xyz.openbmc_project.State.Decorator."
                        "OperationalStatus")
                    {
                        for (const auto& propertyMap : interfaceMap.second)
                        {
                            if (propertyMap.first == "Functional")
                            {
                                const bool* functionalPtr =
                                    std::get_if<bool>(&propertyMap.second);
                                if (functionalPtr == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    break;
                                }
                                state = *functionalPtr;
                            }
                        }
                    }
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LicenseEntry.v1_0_0.LicenseEntry";
                asyncResp->res.jsonValue["@odata.id"] = licenseEntryPath;
                asyncResp->res.jsonValue["Id"] = licenseEntryID;
                asyncResp->res.jsonValue["LicenseType"] = *licenseTypePtr;
                asyncResp->res.jsonValue["SerialNumber"] = *serialNumPtr;
                asyncResp->res.jsonValue["Name"] = *licenseNamePtr;
                asyncResp->res.jsonValue["ExpirationDate"] =
                    crow::utility::getDateTime(expirationTime);
                asyncResp->res.jsonValue["LicenseScope"]["AuthorizationType"] =
                    *authorizationTypePtr;
                asyncResp->res.jsonValue["LicenseScope"]["MaxNumberOfDevices"] =
                    *deviceNumPtr;

                if (available)
                {
                    asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                    if (state)
                    {
                        asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
                    }
                    else
                    {
                        asyncResp->res.jsonValue["Status"]["State"] =
                            "Disabled";
                    }
                }
                else
                {
                    asyncResp->res.jsonValue["Status"]["Health"] = "Critical";
                    asyncResp->res.jsonValue["Status"]["State"] = "Unavailable";
                }
            }
        },
        "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/license",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
}

inline void requestRoutesLicenseEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/LicenseService/Licenses/<str>/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& param) {
                getLicenseEntryById(asyncResp, param);
            });
}
} // namespace redfish
