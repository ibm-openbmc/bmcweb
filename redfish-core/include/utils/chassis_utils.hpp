#pragma once

namespace redfish
{

namespace chassis_utils
{

/**
 * @brief Retrieves valid chassis ID
 * @param asyncResp   Pointer to object holding response data
 * @param callback  Callback for next step to get valid chassis ID
 */
template <typename Callback>
void getValidChassisID(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& chassisID, Callback&& callback)
{
    BMCWEB_LOG_DEBUG << "checkChassisId enter";
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    auto respHandler = [callback{std::move(callback)}, asyncResp, chassisID](
                           const boost::system::error_code ec,
                           const std::vector<std::string>& chassisPaths) {
        BMCWEB_LOG_DEBUG << "getValidChassisID respHandler enter";
        if (ec)
        {
            BMCWEB_LOG_ERROR << "getValidChassisID respHandler DBUS error: "
                             << ec;
            messages::internalError(asyncResp->res);
            return;
        }

        std::optional<std::string> validChassisID;
        std::string chassisName;
        for (const std::string& chassis : chassisPaths)
        {
            sdbusplus::message::object_path path(chassis);
            chassisName = path.filename();
            if (chassisName.empty())
            {
                BMCWEB_LOG_ERROR << "Failed to find chassisName in " << chassis;
                continue;
            }
            if (chassisName == chassisID)
            {
                validChassisID = chassisID;
                break;
            }
        }
        callback(validChassisID);
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
    crow::connections::systemBus->async_method_call(
        [aResp, chassisPath, assemblies, callback{std::move(callback)}](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "D-Bus response error on GetSubTree " << ec;
                messages::internalError(aResp->res);
                return;
            }

            if (subtree.size() == 0)
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

            if (updatedAssemblyList.size() != 0)
            {
                // sorting is required to facilitate patch as the array does not
                // store and data which can be mapped back to Dbus path of
                // assembly.
                std::sort(updatedAssemblyList.begin(),
                          updatedAssemblyList.end());
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
    crow::connections::systemBus->async_method_call(
        [aResp, chassisPath, callback{std::move(callback)}](
            const boost::system::error_code ec,
            const std::variant<std::vector<std::string>>& endpoints) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response "
                                    "error";
                messages::internalError(aResp->res);
                return;
            }

            const std::vector<std::string>* assemblyList =
                std::get_if<std::vector<std::string>>(&(endpoints));

            if (assemblyList == nullptr)
            {
                BMCWEB_LOG_DEBUG << "No assembly found";
                return;
            }

            std::vector<std::string> sortedAssemblyList = *assemblyList;
            std::sort(sortedAssemblyList.begin(), sortedAssemblyList.end());
            checkAssemblyInterface(aResp, chassisPath, sortedAssemblyList,
                                   std::move(callback));
            return;
        },
        "xyz.openbmc_project.ObjectMapper", assemblyPath,
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
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

    crow::connections::systemBus->async_method_call(
        [aResp, chassisPath, callback{std::move(callback)}](
            const boost::system::error_code ec,
            const std::variant<associationList>& associations) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);
                return;
            }

            const associationList* value =
                std::get_if<associationList>(&associations);
            if (value == nullptr)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& listOfAssociations : *value)
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
        },
        service, chassisPath, "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association.Definitions", "Associations");
}

template <typename Callback>
inline void checkAssociation(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& chassisPath,
                             Callback&& callback)
{
    BMCWEB_LOG_DEBUG << "Check chassis for association";

    // check if this chassis hosts any association
    crow::connections::systemBus->async_method_call(
        [aResp, chassisPath, callback{std::move(callback)}](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& [serviceName, interfaceList] : object)
            {
                for (auto& interface : interfaceList)
                {
                    if (interface ==
                        "xyz.openbmc_project.Association.Definitions")
                    {
                        checkForAssemblyAssociations(aResp, chassisPath,
                                                     serviceName,
                                                     std::move(callback));
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

    // get the chassis path
    crow::connections::systemBus->async_method_call(
        [aResp, chassisID, callback{std::move(callback)}](
            const boost::system::error_code ec,
            const std::vector<std::string>& chassisPaths) {
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
