#pragma once
#include <async_resp.hpp>

namespace redfish
{

namespace chassis_utils
{
/**
 * @brief Retrieves valid chassis path
 * @param asyncResp   Pointer to object holding response data
 * @param callback  Callback for next step to get valid chassis path
 */
template <typename Callback>
void getValidChassisPath(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisId, Callback&& callback)
{
    BMCWEB_LOG_DEBUG << "checkChassisId enter";
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    auto respHandler =
        [callback{std::forward<Callback>(callback)}, asyncResp,
         chassisId](const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreePathsResponse&
                        chassisPaths) mutable {
        BMCWEB_LOG_DEBUG << "getValidChassisPath respHandler enter";
        if (ec)
        {
            BMCWEB_LOG_ERROR << "getValidChassisPath respHandler DBUS error: "
                             << ec;
            messages::internalError(asyncResp->res);
            return;
        }

        std::optional<std::string> chassisPath;
        std::string chassisName;
        for (const std::string& chassis : chassisPaths)
        {
            sdbusplus::message::object_path path(chassis);
            chassisName = path.filename();
            if (chassisName.empty())
            {
                BMCWEB_LOG_ERROR << "Failed to find '/' in " << chassis;
                continue;
            }
            if (chassisName == chassisId)
            {
                chassisPath = chassis;
                break;
            }
        }
        callback(chassisPath);
    };

    // Get the Chassis Collection
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0, interfaces);
    BMCWEB_LOG_DEBUG << "checkChassisId exit";
}

template <typename Callback>
inline void
    checkAssemblyInterface(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& chassisPath,
                           std::vector<std::string>& assemblies,
                           Callback&& callback)
{
    constexpr std::array<const char*, 9> chassisAssemblyIfaces = {
        "xyz.openbmc_project.Inventory.Item.Vrm",
        "xyz.openbmc_project.Inventory.Item.Tpm",
        "xyz.openbmc_project.Inventory.Item.Panel",
        "xyz.openbmc_project.Inventory.Item.Battery",
        "xyz.openbmc_project.Inventory.Item.DiskBackplane",
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Connector",
        "xyz.openbmc_project.Inventory.Item.Drive",
        "xyz.openbmc_project.Inventory.Item.Board.Motherboard"};

    crow::connections::systemBus->async_method_call(
        [aResp, chassisPath, assemblies,
         callback{std::forward<Callback>(callback)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "D-Bus response error on GetSubTree " << ec;
            messages::internalError(aResp->res);
            return;
        }

        if (subtree.empty())
        {
            BMCWEB_LOG_DEBUG << "No object paths found";
            return;
        }
        std::vector<std::string> updatedAssemblyList;
        for (const auto& [objectPath, serviceName] : subtree)
        {
            auto it =
                std::find(assemblies.begin(), assemblies.end(), objectPath);
            if (it != assemblies.end())
            {
                updatedAssemblyList.emplace(updatedAssemblyList.end(), *it);
            }
        }

        if (!updatedAssemblyList.empty())
        {
            // Sort the assembly list
            std::sort(updatedAssemblyList.begin(), updatedAssemblyList.end());
            callback(updatedAssemblyList);
        }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", int32_t(0), chassisAssemblyIfaces);
}

template <typename Callback>
inline void
    getAssemblyEndpoints(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& chassisPath, Callback&& callback)
{

    BMCWEB_LOG_DEBUG << "Get assembly endpoints";

    sdbusplus::message::object_path assemblyPath(chassisPath);
    assemblyPath /= "assembly";

    // if there is assembly association, look
    // for endpoints
    dbus::utility::getAssociationEndPoints(
        assemblyPath,
        [aResp, chassisPath, callback{std::forward<Callback>(callback)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperEndPoints& assemblyList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response "
                                "error";
            messages::internalError(aResp->res);
            return;
        }

        dbus::utility::MapperEndPoints sortedAssemblyList = assemblyList;
        std::sort(sortedAssemblyList.begin(), sortedAssemblyList.end());
        checkAssemblyInterface(aResp, chassisPath, sortedAssemblyList,
                               std::move(callback));
        return;
        });
}

template <typename Callback>
inline void checkForAssemblyAssociations(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisPath, const std::string& service,
    Callback&& callback)
{
    BMCWEB_LOG_DEBUG << "Check for assembly association";

    using associationList =
        std::vector<std::tuple<std::string, std::string, std::string>>;

    sdbusplus::asio::getProperty<associationList>(
        *crow::connections::systemBus, service, chassisPath,
        "xyz.openbmc_project.Association.Definitions", "Associations",
        [aResp, chassisPath, callback{std::forward<Callback>(callback)}](
            const boost::system::error_code ec,
            const associationList& associations) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error";
            messages::internalError(aResp->res);
            return;
        }

        for (const auto& listOfAssociations : associations)
        {
            if (std::get<0>(listOfAssociations) != "assembly")
            {
                // implies this is not an assembly
                // association
                continue;
            }

            getAssemblyEndpoints(aResp, chassisPath, std::move(callback));
            break;
        }
        });
}

template <typename Callback>
inline void checkAssociation(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& chassisPath,
                             Callback&& callback)
{
    BMCWEB_LOG_DEBUG << "Check chassis for association";

    // check if this chassis hosts any association
    crow::connections::systemBus->async_method_call(
        [aResp, chassisPath, callback{std::forward<Callback>(callback)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetObject& object) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error";
            messages::internalError(aResp->res);
            return;
        }

        for (const auto& [serviceName, interfaceList] : object)
        {
            for (const auto& interface : interfaceList)
            {
                if (interface == "xyz.openbmc_project.Association.Definitions")
                {
                    checkForAssemblyAssociations(
                        aResp, chassisPath, serviceName, std::move(callback));
                    return;
                }
            }
        }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", chassisPath,
        std::array<const char*, 0>{});
}

template <typename Callback>
inline void getChassisAssembly(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& chassisID,
                               Callback&& callback)
{
    BMCWEB_LOG_DEBUG << "Get chassis path";

    // Get Chassis path
    crow::connections::systemBus->async_method_call(
        [aResp, chassisID, callback{std::forward<Callback>(callback)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse& chassisPaths) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error";
            messages::internalError(aResp->res);
            return;
        }

        // check if the chassis path belongs to the chassis ID passed
        for (const auto& path : chassisPaths)
        {
            BMCWEB_LOG_DEBUG << "Chassis Paths from Mapper " << path;
            std::string chassis =
                sdbusplus::message::object_path(path).filename();
            if (chassis != chassisID)
            {
                // this is not the chassis we are interested in
                continue;
            }

            checkAssociation(aResp, path, std::move(callback));
            return;
        }

        BMCWEB_LOG_ERROR << "Chassis not found";
        messages::resourceNotFound(aResp->res, "Chassis", chassisID);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.Chassis"});
}

} // namespace chassis_utils
} // namespace redfish
