#pragma once

#include "privileges.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"

#include <app.hpp>
#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>

#include <string>

namespace redfish
{
static void handleAcfScriptsGet(
    App& /*unused*/, const crow::Request& /*unused*/,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Handling GET request for ACF scripts");
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const std::vector<std::string>& msg) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& response = asyncResp->res.jsonValue;
            response["@odata.context"] =
                "/redfish/v1/$metadata#AccountService.AcfScripts";
            response["@odata.type"] = "#AccountService.AcfScripts";
            response["Name"] = "ACF Scripts";
            response["Id"] = "AcfScripts";
            response["Description"] =
                "ACF Scripts currently active on the system";
            nlohmann::json scripts;
            std::transform(
                msg.begin(), msg.end(), std::back_inserter(scripts),
                [](const std::string& script) {
                    nlohmann::json scriptJson;
                    scriptJson["Id"] = script; // Use the script ID
                    scriptJson["Actions"] = {
                        {"#AccountService.AcfScripts.Cancel",
                         {{"target",
                           std::format(
                               "/redfish/v1/AccountService/acf/{}/cancel",
                               script)}}}};
                    return scriptJson;
                });
            response["Members"] = scripts;
            response["OdataCount"] = scripts.size();
            response["@odata.id"] = "/redfish/v1/AccountService/acf/scripts";
        },
        "xyz.openbmc_project.acfshell", "/xyz/openbmc_project/acfshell",
        "xyz.openbmc_project.TacfShell", "active");
}
static void handleAcfScriptCancel(
    App& /*unused*/, const crow::Request& /*unused*/,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& scriptId)
{
    BMCWEB_LOG_DEBUG("Handling GET request for ACF scripts");
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec, bool success) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["status"] = success;
        },
        "xyz.openbmc_project.acfshell", "/xyz/openbmc_project/acfshell",
        "xyz.openbmc_project.TacfShell", "cancel", scriptId);
}
inline void requestRoutesAcfService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/acf/scripts/")
        .privileges(redfish::privileges::getAccountService)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleAcfScriptsGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/AccountService/acf/<str>/cancel/")
        .privileges(redfish::privileges::postManagerAccountCollection)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleAcfScriptCancel, std::ref(app)));
}

} // namespace redfish
