#pragma once

#include "common.hpp"
#include "sessions.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/url/url.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace crow
{

struct Request
{
    using http_request_body =
        boost::beast::http::request<boost::beast::http::string_body>;
    std::shared_ptr<http_request_body> reqPtr;
    http_request_body& req;

  private:
    boost::urls::url urlBase{};

  public:
    bool isSecure{false};

    boost::asio::io_context* ioService{};
    boost::asio::ip::address ipAddress{};

    std::shared_ptr<persistent_data::UserSession> session;

    std::string userRole{};
    Request(http_request_body reqIn, std::error_code& ec) :
        reqPtr(std::make_shared<http_request_body>(std::move(reqIn))),
        req(*reqPtr)
    {
        if (!setUrlInfo())
        {
            ec = std::make_error_code(std::errc::invalid_argument);
        }
    }

    Request(std::string_view bodyIn, std::error_code& /*ec*/) :
        reqPtr(
            std::make_shared<http_request_body>(http_request_body({}, bodyIn))),
        req(*reqPtr)
    {}

    Request() : reqPtr(std::make_shared<http_request_body>()), req(*reqPtr) {}

    Request(const Request& other) noexcept :
        reqPtr(other.reqPtr), req(*reqPtr), urlBase(other.urlBase),
        isSecure(other.isSecure), ioService(other.ioService),
        ipAddress(other.ipAddress), session(other.session),
        userRole(other.userRole)
    {
        setUrlInfo();
    }

    Request(Request&& other) noexcept :
        reqPtr(std::move(other.reqPtr)), req(*reqPtr),
        urlBase(std::move(other.urlBase)), isSecure(other.isSecure),
        ioService(other.ioService), ipAddress(std::move(other.ipAddress)),
        session(std::move(other.session)), userRole(std::move(other.userRole))
    {
        setUrlInfo();
    }

    Request& operator=(const Request&) = delete;
    Request& operator=(const Request&&) = delete;
    ~Request() = default;

    boost::beast::http::verb method() const
    {
        return req.method();
    }

    std::string_view getHeaderValue(std::string_view key) const
    {
        return req[key];
    }

    std::string_view getHeaderValue(boost::beast::http::field key) const
    {
        return req[key];
    }

    std::string_view methodString() const
    {
        return req.method_string();
    }

    std::string_view target() const
    {
        return req.target();
    }

    boost::urls::url_view url() const
    {
        return {urlBase};
    }

    const boost::beast::http::fields& fields() const
    {
        return req.base();
    }

    const std::string& body() const
    {
        return req.body();
    }

    bool target(std::string_view target)
    {
        req.target(target);
        return setUrlInfo();
    }

    unsigned version() const
    {
        return req.version();
    }

    bool isUpgrade() const
    {
        return boost::beast::websocket::is_upgrade(req);
    }

    bool keepAlive() const
    {
        return req.keep_alive();
    }

  private:
    bool setUrlInfo()
    {
        auto result = boost::urls::parse_relative_ref(target());

        if (!result)
        {
            return false;
        }
        urlBase = *result;
        return true;
    }
};

} // namespace crow
