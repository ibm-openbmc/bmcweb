// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/bios_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/sw_utils.hpp"

#include <sys/types.h>

#include <boost/beast/http/verb.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>

#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace redfish
{
using BaseTableOption =
    std::tuple<std::string, dbus::utility::DbusVariantType, std::string>;
using BaseTableAttribute =
    std::tuple<std::string, bool, std::string, std::string, std::string,
               dbus::utility::DbusVariantType, dbus::utility::DbusVariantType,
               std::vector<BaseTableOption>>;
using BiosBaseTableItemType = std::pair<std::string, BaseTableAttribute>;
using BiosBaseTableType = std::vector<BiosBaseTableItemType>;

using PendingAttributesItemType =
    std::pair<std::string,
              std::tuple<std::string, dbus::utility::DbusVariantType>>;
using PendingAttributesType = std::vector<PendingAttributesItemType>;

enum PendingAttributesIndex
{
    pendingAttrType = 0,
    pendingAttrValue
};

enum BiosBaseTableIndex
{
    biosBaseAttrType = 0,
    biosBaseReadonlyStatus,
    biosBaseDisplayName,
    biosBaseDescription,
    biosBaseMenuPath,
    biosBaseCurrValue,
    biosBaseDefaultValue,
    biosBaseOptions
};

using OptionsItemType =
    std::tuple<std::string, dbus::utility::DbusVariantType, std::string>;

enum OptionsItemIndex
{
    optItemType = 0,
    optItemValue
};

inline std::string mapAttrTypeToRedfish(const std::string_view typeDbus)
{
    std::string ret;
    if (typeDbus ==
        "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration")
    {
        ret = "Enumeration";
    }
    else if (typeDbus ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.String")
    {
        ret = "String";
    }
    else if (typeDbus ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Password")
    {
        ret = "Password";
    }
    else if (typeDbus ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer")
    {
        ret = "Integer";
    }
    else if (typeDbus ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean")
    {
        ret = "Boolean";
    }
    else
    {
        ret = "UNKNOWN";
    }

    return ret;
}

inline std::string mapBoundTypeToRedfish(const std::string_view typeDbus)
{
    std::string ret;
    if (typeDbus ==
        "xyz.openbmc_project.BIOSConfig.Manager.BoundType.ScalarIncrement")
    {
        ret = "ScalarIncrement";
    }
    else if (typeDbus ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.LowerBound")
    {
        ret = "LowerBound";
    }
    else if (typeDbus ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.UpperBound")
    {
        ret = "UpperBound";
    }
    else if (typeDbus ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.MinStringLength")
    {
        ret = "MinLength";
    }
    else if (typeDbus ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.MaxStringLength")
    {
        ret = "MaxLength";
    }
    else if (typeDbus ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.OneOf")
    {
        ret = "OneOf";
    }
    else
    {
        ret = "UNKNOWN";
    }

    return ret;
}

enum class BaseTableAttributeIndex
{
    Type = 0,
    ReadOnly,
    Name,
    Description,
    Path,
    CurrentValue,
    DefaultValue,
    Options
};

using BaseTable = std::map<std::string, BaseTableAttribute>;

inline void populateRedfishFromBaseTable(crow::Response& response,
                                         const BaseTable& baseTable)
{
    nlohmann::json& attributes = response.jsonValue["Attributes"];
    for (const auto& [name, baseTableAttribute] : baseTable)
    {
        bios_utils::addAttribute(
            attributes, name,
            std::get<uint(BaseTableAttributeIndex::Type)>(baseTableAttribute),
            std::get<uint(BaseTableAttributeIndex::CurrentValue)>(
                baseTableAttribute));
    }
}

inline void handleBiosManagerObjectForGetBiosAttributes(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath)
{
    bios_utils::getBIOSManagerProperty<BaseTable>(
        asyncResp, "BaseBIOSTable", objectPath,
        std::bind_front(populateRedfishFromBaseTable,
                        std::ref(asyncResp->res)));
}

inline void getBiosAttributes(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    bios_utils::getBIOSManagerObject(
        asyncResp, std::bind_front(handleBiosManagerObjectForGetBiosAttributes,
                                   asyncResp));
}

/**
 * BiosService class supports handle get method for bios.
 */
inline void handleBiosServiceGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] = std::format(
        "/redfish/v1/Systems/{}/Bios", BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["@odata.type"] = "#Bios.v1_1_0.Bios";
    asyncResp->res.jsonValue["Name"] = "BIOS Configuration";
    asyncResp->res.jsonValue["Description"] = "BIOS Configuration Service";
    asyncResp->res.jsonValue["Id"] = "BIOS";
    asyncResp->res.jsonValue["Actions"]["#Bios.ResetBios"]["target"] =
        std::format("/redfish/v1/Systems/{}/Bios/Actions/Bios.ResetBios",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["@Redfish.Settings"]["@odata.type"] =
        "#Settings.v1_3_0.Settings";
    asyncResp->res
        .jsonValue["@Redfish.Settings"]["SettingsObject"]["@odata.id"] =
        "/redfish/v1/Systems/system/Bios/Settings";
    dbus::utility::checkDbusPathExists(
        "/xyz/openbmc_project/bios_config/manager", [asyncResp](int rc) {
            if (rc > 0)
            {
                getBiosAttributes(asyncResp);
            }
        });
    // Get the ActiveSoftwareImage and SoftwareImages
    sw_util::populateSoftwareInformation(asyncResp, sw_util::biosPurpose, "",
                                         true);
}

inline void handleBiosAttributeRegistryGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Registries/BiosAttributeRegistry/BiosAttributeRegistry";
    asyncResp->res.jsonValue["@odata.type"] =
        "#AttributeRegistry.v1_3_2.AttributeRegistry";
    asyncResp->res.jsonValue["Name"] = "Bios Attribute Registry";
    asyncResp->res.jsonValue["Id"] = "BiosAttributeRegistry";
    asyncResp->res.jsonValue["RegistryVersion"] = "1.0.0";
    asyncResp->res.jsonValue["Language"] = "en";
    asyncResp->res.jsonValue["OwningEntity"] = "OpenBMC";
    asyncResp->res.jsonValue["RegistryEntries"]["Attributes"] =
        nlohmann::json::array();

    dbus::utility::getProperty<BiosBaseTableType>(
        "xyz.openbmc_project.BIOSConfigManager",
        "/xyz/openbmc_project/bios_config/manager",
        "xyz.openbmc_project.BIOSConfig.Manager", "BaseBIOSTable",
        [asyncResp](const boost::system::error_code& ec,
                    const BiosBaseTableType& baseBiosTable) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("getProperty failed: {}", ec.message());
                messages::resourceNotFound(asyncResp->res, "Registries/Bios",
                                           "Bios");
                return;
            }

            nlohmann::json& attributeArray =
                asyncResp->res.jsonValue["RegistryEntries"]["Attributes"];

            for (const BiosBaseTableItemType& item : baseBiosTable)
            {
                nlohmann::json attributeItem;
                nlohmann::json optionsArray = nlohmann::json::array();

                const std::string& itemType =
                    std::get<biosBaseAttrType>(item.second);
                std::string attrType = mapAttrTypeToRedfish(itemType);
                if (attrType == "UNKNOWN")
                {
                    BMCWEB_LOG_ERROR("attrType == UNKNOWN");
                    messages::internalError(asyncResp->res);
                    return;
                }

                attributeItem["AttributeName"] = item.first;
                attributeItem["Type"] = attrType;
                attributeItem["ReadOnly"] =
                    std::get<biosBaseReadonlyStatus>(item.second);
                attributeItem["DisplayName"] =
                    std::get<biosBaseDisplayName>(item.second);
                attributeItem["HelpText"] =
                    std::get<biosBaseDescription>(item.second);

                if (!std::get<biosBaseMenuPath>(item.second).empty())
                {
                    attributeItem["MenuPath"] =
                        std::get<biosBaseMenuPath>(item.second);
                }

                if (attrType == "String" || attrType == "Enumeration")
                {
                    const std::string* currValue = std::get_if<std::string>(
                        &std::get<biosBaseCurrValue>(item.second));
                    const std::string* defValue = std::get_if<std::string>(
                        &std::get<biosBaseDefaultValue>(item.second));

                    if (currValue && !currValue->empty())
                    {
                        attributeItem["CurrentValue"] = *currValue;
                    }
                    if (defValue && !defValue->empty())
                    {
                        attributeItem["DefaultValue"] = *defValue;
                    }
                }
                else if (attrType == "Integer")
                {
                    const int64_t* currValue = std::get_if<int64_t>(
                        &std::get<biosBaseCurrValue>(item.second));
                    const int64_t* defValue = std::get_if<int64_t>(
                        &std::get<biosBaseDefaultValue>(item.second));

                    attributeItem["CurrentValue"] = currValue ? *currValue : 0;
                    attributeItem["DefaultValue"] = defValue ? *defValue : 0;
                }
                else
                {
                    BMCWEB_LOG_ERROR("Unsupported attribute type.");
                    messages::internalError(asyncResp->res);
                    return;
                }

                const std::vector<OptionsItemType>& optionsVector =
                    std::get<biosBaseOptions>(item.second);
                for (const OptionsItemType& optItem : optionsVector)
                {
                    nlohmann::json optItemJson;
                    const std::string& strOptItemType =
                        std::get<optItemType>(optItem);
                    std::string optItemTypeRedfish =
                        mapBoundTypeToRedfish(strOptItemType);
                    if (optItemTypeRedfish == "UNKNOWN")
                    {
                        BMCWEB_LOG_ERROR("optItemTypeRedfish == UNKNOWN");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    if (optItemTypeRedfish == "OneOf")
                    {
                        const std::string* currValue = std::get_if<std::string>(
                            &std::get<optItemValue>(optItem));
                        if (currValue)
                        {
                            optItemJson["ValueName"] = *currValue;
                            optionsArray.push_back(optItemJson);
                        }
                    }
                    else
                    {
                        const int64_t* currValue = std::get_if<int64_t>(
                            &std::get<optItemValue>(optItem));
                        if (currValue)
                        {
                            attributeItem[optItemTypeRedfish] = *currValue;
                        }
                    }
                }

                if (!optionsArray.empty())
                {
                    attributeItem["Value"] = optionsArray;
                }

                attributeArray.push_back(attributeItem);
            }
        });
}

inline void handleBiosSettingsGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (systemName != "system")
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/system/Bios/Settings";
    asyncResp->res.jsonValue["@odata.type"] = "#Bios.v1_1_0.Bios";
    asyncResp->res.jsonValue["Name"] = "Bios Settings";
    asyncResp->res.jsonValue["Id"] = "BiosSettings";
    asyncResp->res.jsonValue["AttributeRegistry"] = "BiosAttributeRegistry";
    nlohmann::json attributes(nlohmann::json::value_t::object);
    asyncResp->res.jsonValue["Attributes"] = attributes;

    dbus::utility::getProperty<PendingAttributesType>(
        "xyz.openbmc_project.BIOSConfigManager",
        "/xyz/openbmc_project/bios_config/manager",
        "xyz.openbmc_project.BIOSConfig.Manager", "PendingAttributes",
        [asyncResp](const boost::system::error_code& ec,
                    const PendingAttributesType& pendingAttributes) {
            if (ec)
            {
                BMCWEB_LOG_WARNING("getBiosSettings DBUS error: {}", ec);
                messages::resourceNotFound(asyncResp->res,
                                           "Systems/system/Bios", "Settings");
                return;
            }
            nlohmann::json& attributesJson =
                asyncResp->res.jsonValue["Attributes"];
            for (const PendingAttributesItemType& item : pendingAttributes)
            {
                const std::string& key = item.first;
                const std::string& itemType =
                    std::get<pendingAttrType>(item.second);
                std::string attrType = mapAttrTypeToRedfish(itemType);
                if (attrType == "String" || attrType == "Enumeration")
                {
                    const std::string* currValue = std::get_if<std::string>(
                        &std::get<pendingAttrValue>(item.second));
                    attributesJson.emplace(
                        key, currValue != nullptr ? *currValue : "");
                }
                else if (attrType == "Integer")
                {
                    const int64_t* currValue = std::get_if<int64_t>(
                        &std::get<pendingAttrValue>(item.second));
                    attributesJson.emplace(
                        key, currValue != nullptr ? *currValue : 0);
                }
                else
                {
                    BMCWEB_LOG_ERROR("Unsupported attribute type.");
                    messages::internalError(asyncResp->res);
                }
            }
        });
}

inline void handleBiosSettingsPatch(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (systemName != "system")
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    nlohmann::json attrsJson;

    if (!redfish::json_util::readJsonPatch(req, asyncResp->res, "Attributes",
                                           attrsJson))
    {
        return;
    }

    if (attrsJson.is_array())
    {
        BMCWEB_LOG_WARNING(
            "The value for 'Attributes' is in a different format");
        messages::propertyValueFormatError(asyncResp->res, attrsJson.dump(),
                                           "Attributes");
        return;
    }

    dbus::utility::getProperty<BiosBaseTableType>(
        "xyz.openbmc_project.BIOSConfigManager",
        "/xyz/openbmc_project/bios_config/manager",
        "xyz.openbmc_project.BIOSConfig.Manager", "BaseBIOSTable",
        [asyncResp, attrsJson,
         systemName](const boost::system::error_code& ec,
                     const BiosBaseTableType& baseBiosTable) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("getBiosAttributes DBUS error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            boost::container::flat_map<std::string,
                                       std::pair<bool, std::string>>
                biosAttrsType;

            for (const BiosBaseTableItemType& item : baseBiosTable)
            {
                biosAttrsType.try_emplace(
                    item.first,
                    std::make_pair(
                        std::get<biosBaseReadonlyStatus>(item.second),
                        std::get<biosBaseAttrType>(item.second)));
            }

            PendingAttributesType pendingAttributes;
            for (const auto& attrItr : attrsJson.items())
            {
                std::string attrName = attrItr.key();

                BMCWEB_LOG_ERROR("attrName: {}", attrName);
                if (attrName.empty())
                {
                    messages::invalidObject(
                        asyncResp->res,
                        boost::urls::format(
                            "redfish/v1/Systems/systemName/Bios/Settings"));
                    return;
                }
                auto it = biosAttrsType.find(attrName);
                if (it == biosAttrsType.end())
                {
                    messages::propertyUnknown(asyncResp->res, attrName);
                    return;
                }

                bool biosAttrReadOnlyStatus = ((*it).second).first;
                if (biosAttrReadOnlyStatus)
                {
                    BMCWEB_LOG_WARNING(
                        "Attribute Type is ReadOnly. Patch failed!");
                    messages::propertyNotWritable(asyncResp->res, attrName);
                    return;
                }

                std::string biosAttrType = ((*it).second).second;
                if (biosAttrType.empty())
                {
                    BMCWEB_LOG_ERROR("Attribute type not found in BIOS Table");
                    messages::internalError(asyncResp->res);
                    return;
                }

                std::string biosRedfishAttrType =
                    mapAttrTypeToRedfish(biosAttrType);
                if (biosRedfishAttrType == "Integer")
                {
                    if (attrItr.value().type() !=
                        nlohmann::json::value_t::number_unsigned)
                    {
                        BMCWEB_LOG_WARNING("The value must be of type int");
                        std::string val =
                            boost::lexical_cast<std::string>(attrItr.value());
                        messages::propertyValueTypeError(asyncResp->res, val,
                                                         attrName);
                        return;
                    }
                    int64_t attrValue = attrItr.value();
                    pendingAttributes.emplace_back(std::make_pair(
                        attrName, std::make_tuple(biosAttrType, attrValue)));
                }
                else if (biosRedfishAttrType == "String" ||
                         biosRedfishAttrType == "Enumeration" ||
                         biosRedfishAttrType == "Password")
                {
                    if (attrItr.value().type() !=
                        nlohmann::json::value_t::string)
                    {
                        BMCWEB_LOG_WARNING("The value must be of type string");
                        std::string val =
                            boost::lexical_cast<std::string>(attrItr.value());
                        messages::propertyValueTypeError(asyncResp->res, val,
                                                         attrName);
                        return;
                    }
                    std::string attrValue = attrItr.value();
                    pendingAttributes.emplace_back(std::make_pair(
                        attrName, std::make_tuple(biosAttrType, attrValue)));
                }
                else if (biosRedfishAttrType == "Boolean")
                {
                    if (attrItr.value().type() !=
                        nlohmann::json::value_t::boolean)
                    {
                        BMCWEB_LOG_WARNING("The value must be of type bool");
                        std::string val =
                            boost::lexical_cast<std::string>(attrItr.value());
                        messages::propertyValueTypeError(asyncResp->res, val,
                                                         attrName);
                        return;
                    }
                    bool attrValue = attrItr.value();
                    pendingAttributes.emplace_back(std::make_pair(
                        attrName, std::make_tuple(biosAttrType, attrValue)));
                }
                else
                {
                    BMCWEB_LOG_ERROR("Attribute Type in BiosTable is Unknown");
                    messages::internalError(asyncResp->res);
                    return;
                }
            }

            sdbusplus::asio::setProperty(
                *crow::connections::systemBus,
                "xyz.openbmc_project.BIOSConfigManager",
                "/xyz/openbmc_project/bios_config/manager",
                "xyz.openbmc_project.BIOSConfig.Manager", "PendingAttributes",
                pendingAttributes,
                [asyncResp](const boost::system::error_code& ec1) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR("doPatch resp_handler got error: {}",
                                         ec1);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                });
        });
}

inline void requestRoutesBiosService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Bios/")
        .privileges(redfish::privileges::getBios)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleBiosServiceGet, std::ref(app)));
}

inline void requestRoutesBiosSettings(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Bios/Settings")
        .privileges(redfish::privileges::getBios)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleBiosSettingsGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Bios/Settings")
        .privileges(redfish::privileges::patchBios)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleBiosSettingsPatch, std::ref(app)));
}
/**
 * BiosReset class supports handle POST method for Reset bios.
 * The class retrieves and sends data directly to D-Bus.
 *
 * Function handles POST method request.
 * Analyzes POST body message before sends Reset request data to D-Bus.
 */
inline void handleBiosResetPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to reset bios: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
        },
        "org.open_power.Software.Host.Updater", "/xyz/openbmc_project/software",
        "xyz.openbmc_project.Common.FactoryReset", "Reset");
}

inline void requestRoutesBiosReset(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Bios/Actions/Bios.ResetBios/")
        .privileges(redfish::privileges::postBios)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleBiosResetPost, std::ref(app)));
}

} // namespace redfish
