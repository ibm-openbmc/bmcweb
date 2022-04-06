#pragma once

namespace redfish
{
inline void requestRoutesPowerSupplyMetrics(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PowerSubsystem/"
                      "PowerSupplies/<str>/Metrics")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& param, const std::string& param2) {
                const std::string& chassisId = param;
                const std::string& powerSupplyId = param2;

                BMCWEB_LOG_INFO << "ChassisID: " << chassisId;
                BMCWEB_LOG_INFO << "PowerSupplyID: " << powerSupplyId;

                asyncResp->res.jsonValue["Name"] =
                    "Metrics for " + powerSupplyId;
            });
}
} // namespace redfish
