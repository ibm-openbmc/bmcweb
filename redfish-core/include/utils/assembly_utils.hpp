// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "boost_formatters.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "human_sort.hpp"
#include "logging.hpp"
#include "utils/chassis_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace redfish
{

static constexpr std::array<std::string_view, 9> assemblyInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Battery",
    "xyz.openbmc_project.Inventory.Item.Board",
    "xyz.openbmc_project.Inventory.Item.Board.Motherboard",
    "xyz.openbmc_project.Inventory.Item.Connector",
    "xyz.openbmc_project.Inventory.Item.DiskBackplane",
    "xyz.openbmc_project.Inventory.Item.Drive",
    "xyz.openbmc_project.Inventory.Item.Panel",
    "xyz.openbmc_project.Inventory.Item.Tpm",
    "xyz.openbmc_project.Inventory.Item.Vrm"};

namespace assembly_utils
{

inline void afterGetChassisAssembly(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::function<void(const boost::system::error_code&,
                       const std::vector<std::string>& sortedAssemblyList)>&
        callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& subtreePaths)
{
    if (ec)
    {
        if (ec.value() == boost::system::errc::io_error || ec.value() == EBADR)
        {
            // Not found
            callback(ec, std::vector<std::string>());
            return;
        }

        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    std::vector<std::string> sortedAssemblyList = subtreePaths;
    std::ranges::sort(sortedAssemblyList, AlphanumLess<std::string>());

    callback(ec, sortedAssemblyList);
}

/**
 * @brief Get chassis path with given chassis ID
 * @param[in] asyncResp - Shared pointer for asynchronous calls.
 * @param[in] chassisId - Chassis to which the assemblies are
 * associated.
 * @param[in] callback
 *
 * @return None.
 */
inline void getChassisAssembly(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    std::function<void(const boost::system::error_code& ec,
                       const std::vector<std::string>& sortedAssemblyList)>&&
        callback)
{
    BMCWEB_LOG_DEBUG("Get ChassisAssembly");

    dbus::utility::getAssociatedSubTreePathsById(
        chassisId, "/xyz/openbmc_project/inventory", chassisInterfaces,
        "assembly", assemblyInterfaces,
        std::bind_front(afterGetChassisAssembly, asyncResp,
                        std::move(callback)));
}

/**
 * @brief API used to fill the Assembly id of the assembled object that
 *        assembled in the given assembly parent object path.
 *
 *        bmcweb using the sequential numeric value by sorting the
 *        assembled objects instead of the assembled object dbus id
 *        for the Redfish Assembly implementation.
 *
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] assemblyParentServ - The assembly parent dbus service name.
 * @param[in] assemblyParentObjPath - The assembly parent dbus object path.
 * @param[in] assemblyParentIface - The assembly parent dbus interface name
 *                                  to valid the supports in the bmcweb.
 * @param[in] assemblyUriPropPath - The redfish property path to fill with id.
 * @param[in] assembledObjPath - The assembled object that need to fill with
 *                               its id. Used to check in the parent assembly
 *                               associations.
 * @param[in] assembledUriVal - The assembled object redfish uri value that
 *                              need to replace with its id.
 *
 * @return The redfish response with assembled object id in the given
 *         redfish property path if success else returns the error.
 */
inline void fillWithAssemblyId(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& assemblyParentServ,
    const sdbusplus::message::object_path& assemblyParentObjPath,
    const std::string& assemblyParentIface,
    const nlohmann::json::json_pointer& assemblyUriPropPath,
    const sdbusplus::message::object_path& assembledObjPath,
    const std::string& assembledUriVal)
{
    if (assemblyParentIface != "xyz.openbmc_project.Inventory.Item.Chassis")
    {
        // Currently, bmcweb supporting only chassis assembly uri so return
        // error if unsupported assembly uri interface was given
        BMCWEB_LOG_ERROR(
            "Unsupported interface [{}] was given to fill assembly id. Please add support in the bmcweb",
            assemblyParentIface);
        messages::internalError(asyncResp->res);
        return;
    }

    using associationList =
        std::vector<std::tuple<std::string, std::string, std::string>>;

    sdbusplus::asio::getProperty<associationList>( //
        *crow::connections::systemBus, assemblyParentServ,
        assemblyParentObjPath.str,
        "xyz.openbmc_project.Association.Definitions", "Associations",
        [asyncResp, assemblyUriPropPath, assemblyParentObjPath,
         assembledObjPath,
         assembledUriVal](const boost::system::error_code& ec,
                          const associationList& associations) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error [{}  : {}] when tried to get the Associations from [{}] to fill Assembly id of the assembled object [{}]",
                    ec.value(), ec.message(), assemblyParentObjPath.str,
                    assembledObjPath.str);
                messages::internalError(asyncResp->res);
                return;
            }

            std::vector<std::string> assemblyAssoc;
            for (const auto& association : associations)
            {
                if (std::get<0>(association) != "assembly")
                {
                    continue;
                }
                assemblyAssoc.emplace_back(std::get<2>(association));
            }

            if (assemblyAssoc.empty())
            {
                BMCWEB_LOG_ERROR(
                    "No assembly associations in the [{}] to fill Assembly id of the assembled object [{}]",
                    assemblyParentObjPath.str, assembledObjPath.str);
                messages::internalError(asyncResp->res);
                return;
            }

            // Mak sure whether the retrieved assembly associations are
            // implemented before finding the assembly id as per bmcweb Assembly
            // design.
            dbus::utility::getSubTree(
                "/xyz/openbmc_project/inventory", 0, chassisAssemblyInterfaces,
                [asyncResp, assemblyUriPropPath, assemblyParentObjPath,
                 assembledObjPath, assemblyAssoc, assembledUriVal](
                    const boost::system::error_code& ec1,
                    const dbus::utility::MapperGetSubTreeResponse& objects) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error [{} : {}] when tried to get the subtree to check assembled objects implementation of the [{}] to find assembled object id of the [{}] to fill in the URI property",
                            ec1.value(), ec1.message(),
                            assemblyParentObjPath.str, assembledObjPath.str);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    if (objects.empty())
                    {
                        BMCWEB_LOG_ERROR(
                            "No objects in the [{}] to check assembled objects implementation to fill the assembled object [{}] id in the URI property",
                            assemblyParentObjPath.str, assembledObjPath.str);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    std::vector<std::string> implAssemblyAssocs;
                    for (const auto& object : objects)
                    {
                        auto it =
                            std::ranges::find(assemblyAssoc, object.first);
                        if (it != assemblyAssoc.end())
                        {
                            implAssemblyAssocs.emplace_back(*it);
                        }
                    }

                    if (implAssemblyAssocs.empty())
                    {
                        BMCWEB_LOG_ERROR(
                            "The assembled objects of the [{}] are not implemented so unable to fill the assembled object [{}] id in the URI property",
                            assemblyParentObjPath.str, assembledObjPath.str);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    // sort  the implemented assemply object as per bmcweb
                    // design to match with Assembly GET and PATCH handler.
                    std::ranges::sort(implAssemblyAssocs);

                    auto assembledObjectIt = std::ranges::find(
                        implAssemblyAssocs, assembledObjPath.str);

                    if (assembledObjectIt == implAssemblyAssocs.end())
                    {
                        BMCWEB_LOG_ERROR(
                            "The assembled object [{}] in the object [{}] is not implemented so unable to fill assembled object id in the URI property",
                            assembledObjPath.str, assemblyParentObjPath.str);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    auto assembledObjectId = std::distance(
                        implAssemblyAssocs.begin(), assembledObjectIt);

                    std::string::size_type assembledObjectNamePos =
                        assembledUriVal.rfind(assembledObjPath.filename());

                    if (assembledObjectNamePos == std::string::npos)
                    {
                        BMCWEB_LOG_ERROR(
                            "The assembled object name [{}] is not found in the redfish property value [{}] to replace with assembled object id [{}]",
                            assembledObjPath.filename(), assembledUriVal,
                            assembledObjectId);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::string uriValwithId(assembledUriVal);
                    uriValwithId.replace(assembledObjectNamePos,
                                         assembledObjPath.filename().length(),
                                         std::to_string(assembledObjectId));

                    asyncResp->res.jsonValue[assemblyUriPropPath] =
                        uriValwithId;
                });
        });
}

} // namespace assembly_utils
} // namespace redfish
