#pragma once

namespace redfish
{
namespace error_log_utils
{

/*
 * @brief The helper API to set the Redfish error log URI in the given
 *        Redfish property JSON path based on the Hidden property
 *        which will be present in the given error log D-Bus object.
 *
 * @param[in] aResp - The redfish response to return.
 * @param[in] errorLogObjPath - The error log D-Bus object path.
 * @param[in] errorLogPropPath - The Redfish property json path to fill URI.
 * @param[in] isLink - The boolean to add URI as a Redfish link.
 *
 * @return NULL
 *
 * @note The "isLink" parameter is used to add the URI as a link (i.e with
 *       "@odata.id"). If passed as "false" then, the suffix will be added
 *       as "/attachment" along with the URI.
 *
 *       This API won't fill the given "errorLogPropPath" property if unable
 *       to process the given error log D-Bus object since the error log
 *       might delete by the user via Redfish but, we should not throw
 *       internal error in that case, just log trace and return.
 */
inline void setErrorLogUri(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const sdbusplus::message::object_path& errorLogObjPath,
    const nlohmann::json_pointer<nlohmann::json>& errorLogPropPath,
    const bool isLink)
{
    // Get the Hidden Property
    crow::connections::systemBus->async_method_call(
        [aResp, errorLogObjPath, errorLogPropPath,
         isLink](const boost::system::error_code ec,
                 std::variant<bool>& hiddenProperty) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DBus response error [" << ec.value()
                                 << " : " << ec.message()
                                 << "] when tried to get the Hidden property "
                                 << "from the given error log object "
                                 << errorLogObjPath.str;
                return;
            }
            bool* hiddenPropVal = std::get_if<bool>(&hiddenProperty);
            if (hiddenPropVal == nullptr)
            {
                BMCWEB_LOG_ERROR << "Failed to get the Hidden property value "
                                 << "from the given error log object "
                                 << errorLogObjPath.str;
                return;
            }

            std::string errLogUri{"/redfish/v1/Systems/system/LogServices/"};
            if (*hiddenPropVal)
            {
                errLogUri.append("CELog/Entries/");
            }
            else
            {
                errLogUri.append("EventLog/Entries/");
            }
            errLogUri.append(errorLogObjPath.filename());

            if (isLink)
            {
                aResp->res.jsonValue[errorLogPropPath] = {
                    {"@odata.id", errLogUri}};
            }
            else
            {
                errLogUri.append("/attachment");
                aResp->res.jsonValue[errorLogPropPath] = errLogUri;
            }
        },
        "xyz.openbmc_project.Logging", errorLogObjPath.str,
        "org.freedesktop.DBus.Properties", "Get",
        "org.open_power.Logging.PEL.Entry", "Hidden");
}

} // namespace error_log_utils
} // namespace redfish
