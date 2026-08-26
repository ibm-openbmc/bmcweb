// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2018 Intel Corporation
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/resource.hpp"
#include "http_request.hpp"
#include "human_sort.hpp"
#include "logging.hpp"
#include "processor.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/hw_isolation.hpp"
#include "utils/json_utils.hpp"
#include "utils/name_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{

// Interfaces which imply a D-Bus object represents a Processor Core
constexpr std::array<std::string_view, 1> procCoreInterfaces = {
    "xyz.openbmc_project.Inventory.Item.CpuCore"};

inline void handleProcessorPaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId,
    const std::function<void(const std::string& cpuPath)>& handler,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& subTreePaths)
{
    if (ec)
    {
        // No processor objects found by mapper
        if (ec.value() == boost::system::errc::io_error)
        {
            BMCWEB_LOG_DEBUG("No processors found");
            handler("");
            return;
        }

        BMCWEB_LOG_ERROR("DBUS response error: {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }

    auto foundCpuPath = std::ranges::find_if(
        subTreePaths, [processorId](const std::string& cpuPath) {
            return isProcObjectMatched(
                processorId, sdbusplus::message::object_path(cpuPath));
        });

    if (foundCpuPath == subTreePaths.end())
    {
        BMCWEB_LOG_DEBUG("Processor {} not found", processorId);
        handler("");
        return;
    }

    handler(*foundCpuPath);
}

inline void getProcessorPaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId,
    std::function<void(const std::string& cpuPath)>&& handler)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Cpu"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, processorId, handler{std::move(handler)}](
            const boost::system::error_code& ec,
            const std::vector<std::string>& subTreePaths) {
            handleProcessorPaths(asyncResp, processorId, handler, ec,
                                 subTreePaths);
        });
}

inline void handleSubProcessorCoreHead(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /* systemName */, const std::string& /* processorId */,
    const std::string& /* coreId */)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/Processor/Processor.json>; rel=describedby");
}

inline void getSubProcessorCoreHealth(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    dbus::utility::getProperty<bool>(
        service, objPath,
        "xyz.openbmc_project.State.Decorator.OperationalStatus", "Functional",
        [asyncResp](const boost::system::error_code& ec, bool functional) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR("DBUS response error, ec: {}", ec.value());
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            if (!functional)
            {
                asyncResp->res.jsonValue["Status"]["Health"] =
                    resource::Health::Critical;
            }
        });
}

inline void afterGetSubProcessorCorePresent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, bool present)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error for Available {}",
                             ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    if (!present)
    {
        asyncResp->res.jsonValue["Status"]["State"] = resource::State::Absent;
        return;
    }
}

inline void afterGetSubProcessorCoreAvailable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& corePath,
    const boost::system::error_code& ec, bool available)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error, ec: {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    if (!available)
    {
        asyncResp->res.jsonValue["Status"]["State"] =
            resource::State::UnavailableOffline;
    }

    dbus::utility::getProperty<bool>(
        service, corePath, "xyz.openbmc_project.Inventory.Item", "Present",
        std::bind_front(afterGetSubProcessorCorePresent, asyncResp));
}

inline void getSubProcessorCoreState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& corePath)
{
    dbus::utility::getProperty<bool>(
        service, corePath, "xyz.openbmc_project.State.Decorator.Availability",
        "Available",
        std::bind_front(afterGetSubProcessorCoreAvailable, asyncResp, service,
                        corePath));
}

inline void getEnabledStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath,
    const std::string& interface)
{
    dbus::utility::getProperty<bool>(
        service, objPath, interface, "Enabled",
        [asyncResp](const boost::system::error_code& ec, bool enabled) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR("DBUS response error, ec: {}", ec.value());
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            asyncResp->res.jsonValue["Enabled"] = enabled;
        });
}

inline void getSubProcessorCoreData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& processorId,
    const std::string& coreId, const std::string& corePath,
    const dbus::utility::MapperServiceMap& object)
{
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/Processor/Processor.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] = "#Processor.v1_18_0.Processor";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Processors/{}/SubProcessors/{}", systemName,
        processorId, coreId);
    asyncResp->res.jsonValue["Name"] = "SubProcessor";
    asyncResp->res.jsonValue["Id"] = coreId;

    asyncResp->res.jsonValue["Status"]["State"] = resource::State::Enabled;
    asyncResp->res.jsonValue["Status"]["Health"] = resource::Health::OK;

    for (const auto& [service, interfaces] : object)
    {
        for (const auto& intf : interfaces)
        {
            if (intf == "xyz.openbmc_project.Inventory.Item")
            {
                name_util::getPrettyName(asyncResp, corePath, service,
                                         "/Name"_json_pointer);
            }
            else if (intf == "xyz.openbmc_project.Object.Enable")
            {
                getEnabledStatus(asyncResp, service, corePath, intf);
            }
        }

        getSubProcessorCoreState(asyncResp, service, corePath);
        getSubProcessorCoreHealth(asyncResp, service, corePath);
    }

    if constexpr (BMCWEB_HW_ISOLATION)
    {
        // Check for the hardware status event
        hw_isolation_utils::getHwIsolationStatus(asyncResp, corePath);
    }
}

inline void doHandleSubProcessorCoreGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& processorId,
    const std::string& coreId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& coreSubTree)
{
    if (ec)
    {
        if (ec.value() == boost::system::errc::io_error)
        {
            BMCWEB_LOG_WARNING("Processor {} not found.", processorId);
            messages::resourceNotFound(asyncResp->res, "Processor",
                                       processorId);
            return;
        }
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }

    for (const auto& [corePath, object] : coreSubTree)
    {
        if (sdbusplus::message::object_path(corePath).filename() == coreId)
        {
            getSubProcessorCoreData(asyncResp, systemName, processorId, coreId,
                                    corePath, object);
            return;
        }
    }

    BMCWEB_LOG_WARNING("Core {} not found.", coreId);
    messages::resourceNotFound(asyncResp->res, "Processor", coreId);
}

inline void handleSubProcessorCoreGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& processorId,
    const std::string& coreId)
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

    getProcessorPaths(
        asyncResp, processorId,
        [asyncResp, systemName, processorId,
         coreId](const std::string& cpuPath) {
            if (cpuPath.empty())
            {
                messages::resourceNotFound(asyncResp->res, "Processor",
                                           processorId);
                return;
            }
            dbus::utility::getAssociatedSubTree(
                cpuPath + "/containing",
                sdbusplus::message::object_path(
                    "/xyz/openbmc_project/inventory"),
                0, procCoreInterfaces,
                std::bind_front(doHandleSubProcessorCoreGet, asyncResp,
                                systemName, processorId, coreId));
        });
}

inline void handleSubProcessorCollectionHead(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& /* processorId */)
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

    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ProcessorCollection/ProcessorCollection.json>; rel=describedby");
}

inline void doHandleSubProcessorCollectionGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& processorId,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& coreSubTreePaths)
{
    if (ec)
    {
        if (ec.value() != boost::system::errc::io_error)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
            messages::internalError(asyncResp->res);
            return;
        }
        BMCWEB_LOG_WARNING("Processor {} not found.", processorId);
        messages::resourceNotFound(asyncResp->res, "Processor", processorId);
        return;
    }

    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ProcessorCollection/ProcessorCollection.json>; rel=describedby");

    asyncResp->res.jsonValue["@odata.type"] =
        "#ProcessorCollection.ProcessorCollection";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Processors/{}/SubProcessors",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, processorId);
    asyncResp->res.jsonValue["Name"] = "SubProcessor Collection";

    asyncResp->res.jsonValue["Members"] = nlohmann::json::array();

    std::vector<std::string> coreIdNames;
    for (const std::string& corePath : coreSubTreePaths)
    {
        std::string coreId =
            sdbusplus::message::object_path(corePath).filename();
        if (!coreId.empty())
        {
            coreIdNames.emplace_back(std::move(coreId));
        }
    }

    std::ranges::sort(coreIdNames, AlphanumLess<std::string>());

    nlohmann::json& members = asyncResp->res.jsonValue["Members"];
    for (const std::string& coreId : coreIdNames)
    {
        nlohmann::json item;
        item["@odata.id"] = boost::urls::format(
            "/redfish/v1/Systems/{}/Processors/{}/SubProcessors/{}", systemName,
            processorId, coreId);
        members.emplace_back(std::move(item));
    }
    asyncResp->res.jsonValue["Members@odata.count"] = members.size();
}

inline void handleSubProcessorCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& processorId)
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

    getProcessorPaths(
        asyncResp, processorId,
        [asyncResp, systemName, processorId](const std::string& cpuPath) {
            dbus::utility::getAssociatedSubTreePaths(
                cpuPath + "/containing",
                sdbusplus::message::object_path(
                    "/xyz/openbmc_project/inventory"),
                0, procCoreInterfaces,
                std::bind_front(doHandleSubProcessorCollectionGet, asyncResp,
                                systemName, processorId));
        });
}

/**
 * @brief API used to process the Processor Core "Enabled" member which is
 *        patched to do appropriate action.
 *
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] procObjPath - The parent processor object path.
 * @param[in] coreId - The patched Processor Core resource id.
 * @param[in] enabled - The patched "Enabled" member value.
 *
 * @return The redfish response in the given buffer.
 *
 * @note - The "Enabled" member of the Processor Core is used to enable
 *         (aka isolate) or disable (aka deisolate) the resource from the
 *         system boot so this function will call
 * "processHardwareIsolationReq" function which is used to handle the
 * resource isolation request.
 *       - The "Enabled" member of the Processor Core is mapped with
 *         "xyz.openbmc_project.Object.Enable::Enabled" dbus property.
 */
inline void patchCpuCoreMemberEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& procObjPath, const std::string& coreId,
    const bool enabled)
{
    redfish::hw_isolation_utils::processHardwareIsolationReq(
        resp, "Core", coreId, enabled,
        std::vector<std::string_view>(procCoreInterfaces.begin(),
                                      procCoreInterfaces.end()),
        procObjPath);
}

/**
 * @brief API used to process the Processor Core members which are tried to
 *        patch.
 *
 * @param[in] req - The redfish patched request to identify the patched
 * members
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] processorId - The patched Core Processor resource id (unused
 * now)
 * @param[in] coreId - The patched Processor Core resource id.
 *
 * @return The redfish response in the given buffer.
 *
 * @note This function will call the appropriate function to handle the
 * patched members of the Processor Core.
 */
inline void patchCpuCoreMembers(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& processorId,
    const std::string& coreId)
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

    std::optional<bool> enabled;

    if (!json_util::readJsonPatch(req, asyncResp->res, "Enabled", enabled))
    {
        return;
    }

    getProcessorPaths(
        asyncResp, processorId,
        [asyncResp, coreId, enabled](const std::string& cpuPath) {
            // Handle patched Enabled Redfish property
            if (enabled.has_value())
            {
                patchCpuCoreMemberEnabled(asyncResp, cpuPath, coreId, *enabled);
            }
        });
}

inline void requestRoutesSubProcessors(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/Processors/<str>/SubProcessors/<str>/")
        .privileges(redfish::privileges::headProcessor)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleSubProcessorCoreHead, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/Processors/<str>/SubProcessors/<str>/")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSubProcessorCoreGet, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Processors/<str>/SubProcessors/")
        .privileges(redfish::privileges::headProcessorCollection)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleSubProcessorCollectionHead, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Processors/<str>/SubProcessors/")
        .privileges(redfish::privileges::getProcessorCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSubProcessorCollectionGet, std::ref(app)));
}

} // namespace redfish
