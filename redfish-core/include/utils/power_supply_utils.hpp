#pragma once

namespace redfish
{

namespace power_supply_utils
{

template <typename Callback>
inline void
    getValidPowerSupplyID(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisID,
                          const std::string& powerSupplyID, Callback&& callback)
{
    BMCWEB_LOG_DEBUG << "getValidPowerSupplyID enter";

    auto respHandler =
        [callback{std::move(callback)}, asyncResp, chassisID, powerSupplyID](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            BMCWEB_LOG_DEBUG << "getValidPowerSupplyID respHandler enter";

            if (ec)
            {
                BMCWEB_LOG_ERROR
                    << "getValidPowerSupplyID respHandler DBUS error: " << ec;
                messages::internalError(asyncResp->res);
                return;
            }

            // Set the default value to resourceNotFound, and if we confirm that
            // powerSupplyID is correct, the error response will be cleared.
            messages::resourceNotFound(asyncResp->res, "PowerSupply",
                                       powerSupplyID);

            for (const auto& object : subtree)
            {
                // The association of this PowerSupply is used to determine
                // whether it belongs to this ChassisID
                crow::connections::systemBus->async_method_call(
                    [callback{std::move(callback)}, asyncResp, chassisID,
                     powerSupplyID,
                     object](const boost::system::error_code ec,
                             const std::variant<std::vector<std::string>>&
                                 endpoints) {
                        if (ec)
                        {
                            if (ec.value() == EBADR)
                            {
                                // This PowerSupply have no chassis association.
                                return;
                            }

                            BMCWEB_LOG_ERROR << "DBUS response error";
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        const std::vector<std::string>* powerSupplyChassis =
                            std::get_if<std::vector<std::string>>(&(endpoints));

                        if (powerSupplyChassis != nullptr)
                        {
                            if ((*powerSupplyChassis).size() != 1)
                            {
                                BMCWEB_LOG_ERROR
                                    << "PowerSupply association error! ";
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            std::vector<std::string> chassisPath =
                                *powerSupplyChassis;
                            sdbusplus::message::object_path path(
                                chassisPath[0]);
                            std::string chassisName = path.filename();
                            if (chassisName != chassisID)
                            {
                                // The PowerSupply does't belong to the
                                // chassisID
                                return;
                            }

                            sdbusplus::message::object_path pathPS(
                                object.first);
                            const std::string powerSupplyName =
                                pathPS.filename();
                            if (powerSupplyName.empty())
                            {
                                BMCWEB_LOG_ERROR
                                    << "Failed to find powerSupplyName in "
                                    << object.first;
                                return;
                            }

                            std::string validPowerSupplyPath;

                            if (powerSupplyName == powerSupplyID)
                            {
                                // Clear resourceNotFound response
                                asyncResp->res.clear();

                                if (object.second.size() != 1)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "Error getting PowerSupply "
                                           "D-Bus object!";
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                const std::string& path = object.first;
                                const std::string& connectionName =
                                    object.second[0].first;

                                callback(path, connectionName);
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    object.first + "/chassis",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        };

    // Get the PowerSupply Collection
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.PowerSupply"});
    BMCWEB_LOG_DEBUG << "getValidPowerSupplyID exit";
}

} // namespace power_supply_utils
} // namespace redfish
