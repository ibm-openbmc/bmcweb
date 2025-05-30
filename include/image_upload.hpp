// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "http_request.hpp"
#include "io_context_singleton.hpp"
#include "logging.hpp"
#include "ossl_random.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <ranges>
#include <string>

namespace crow
{
namespace image_upload
{

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match_t> fwUpdateMatcher;

inline void uploadImageHandler(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Only allow one FW update at a time
    if (fwUpdateMatcher != nullptr)
    {
        asyncResp->res.addHeader("Retry-After", "30");
        asyncResp->res.result(boost::beast::http::status::service_unavailable);
        return;
    }
    // Make this const static so it survives outside this method
    static boost::asio::steady_timer timeout(getIoContext(),
                                             std::chrono::seconds(5));

    timeout.expires_after(std::chrono::seconds(15));

    auto timeoutHandler = [asyncResp](const boost::system::error_code& ec) {
        fwUpdateMatcher = nullptr;
        if (ec == boost::asio::error::operation_aborted)
        {
            // expected, we were canceled before the timer completed.
            return;
        }
        BMCWEB_LOG_ERROR("Timed out waiting for Version interface");

        if (ec)
        {
            BMCWEB_LOG_ERROR("Async_wait failed {}", ec);
            return;
        }

        asyncResp->res.result(boost::beast::http::status::bad_request);
        asyncResp->res.jsonValue["data"]["description"] =
            "Version already exists or failed to be extracted";
        asyncResp->res.jsonValue["message"] = "400 Bad Request";
        asyncResp->res.jsonValue["status"] = "error";
    };

    std::function<void(sdbusplus::message_t&)> callback =
        [asyncResp](sdbusplus::message_t& m) {
            BMCWEB_LOG_DEBUG("Match fired");

            sdbusplus::message::object_path path;
            dbus::utility::DBusInterfacesMap interfaces;
            m.read(path, interfaces);

            if (std::ranges::find_if(interfaces, [](const auto& i) {
                    return i.first == "xyz.openbmc_project.Software.Version";
                }) != interfaces.end())
            {
                timeout.cancel();
                std::string leaf = path.filename();
                if (leaf.empty())
                {
                    leaf = path.str;
                }

                asyncResp->res.jsonValue["data"] = leaf;
                asyncResp->res.jsonValue["message"] = "200 OK";
                asyncResp->res.jsonValue["status"] = "ok";
                BMCWEB_LOG_DEBUG("ending response");
                fwUpdateMatcher = nullptr;
            }
        };
    fwUpdateMatcher = std::make_unique<sdbusplus::bus::match_t>(
        *crow::connections::systemBus,
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',path='/xyz/openbmc_project/software'",
        callback);

    std::string filepath("/tmp/images/" + bmcweb::getRandomUUID());
    BMCWEB_LOG_DEBUG("Writing file to {}", filepath);
    std::ofstream out(filepath, std::ofstream::out | std::ofstream::binary |
                                    std::ofstream::trunc);
    out << req.body();
    out.close();
    timeout.async_wait(timeoutHandler);
}

inline void requestRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/upload/image/<str>")
        .privileges({{"ConfigureComponents", "ConfigureManager"}})
        .methods(boost::beast::http::verb::post, boost::beast::http::verb::put)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string&) { uploadImageHandler(req, asyncResp); });

    BMCWEB_ROUTE(app, "/upload/image")
        .privileges({{"ConfigureComponents", "ConfigureManager"}})
        .methods(boost::beast::http::verb::post, boost::beast::http::verb::put)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                uploadImageHandler(req, asyncResp);
            });
}
} // namespace image_upload
} // namespace crow
