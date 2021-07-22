#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "http_request.hpp"
#include "registries/privilege_registry.hpp"

#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace redfish
{

/****************************************************
 * Redfish HardwareIsolationLog interfaces
 ******************************************************/

/**
 * @brief API Used to add the supported HardwareIsolation LogServices Members
 *
 * @param[in] req - The HardwareIsolation redfish request (unused now).
 * @param[in] asyncResp - The redfish response to return.
 *
 * @return The redfish response in the given buffer.
 */
inline void getSystemHardwareIsolationLogService(
    const crow::Request& /* req */,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/LogServices/HardwareIsolation",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["@odata.type"] = "#LogService.v1_2_0.LogService";
    asyncResp->res.jsonValue["Name"] = "Hardware Isolation LogService";
    asyncResp->res.jsonValue["Description"] =
        "Hardware Isolation LogService for system owned devices";
    asyncResp->res.jsonValue["Id"] = "HardwareIsolation";

    asyncResp->res.jsonValue["Entries"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/LogServices/HardwareIsolation/Entries",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);

    asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Systems/{}/LogServices/HardwareIsolation/Actions/LogService.ClearLog",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
}

/**
 * @brief API used to route the handler for HardwareIsolation Redfish
 *        LogServices URI
 *
 * @param[in] app - Crow app on which Redfish will initialize
 *
 * @return The appropriate redfish response for the given redfish request.
 */
inline void requestRoutesSystemHardwareIsolationLogService(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/system/LogServices/HardwareIsolation/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            getSystemHardwareIsolationLogService);
}

} // namespace redfish
