#pragma once

#include "http_request.hpp"

#include <boost/beast/http/verb.hpp>

#include <cstring>
#include <string>

namespace audit
{

int auditGetFD();
void auditClose();
bool auditOpen();
bool auditReopen();
void auditSetState(bool enable);
bool wantDetail(const crow::Request& req);
bool appendItemToBuf(std::string& strBuf, size_t maxBufSize,
                     const std::string& item);

/**
 * @brief Checks if POST request is a user connection event
 *
 * Login and Session requests are audited when the authentication is attempted.
 * This allows failed requests to be audited with the user detail.
 *
 * @return True if request is a user connection event
 */
inline bool checkPostUser(const crow::Request& req)
{
    return (req.target() == "/redfish/v1/SessionService/Sessions") ||
           (req.target() == "/redfish/v1/SessionService/Sessions/") ||
           (req.target() == "/login");
}

/**
 * @brief Checks if request should be audited after completion
 * @return  True if request should be audited
 */
inline bool wantAudit(const crow::Request& req)
{
    return (req.method() == boost::beast::http::verb::patch) ||
           (req.method() == boost::beast::http::verb::put) ||
           (req.method() == boost::beast::http::verb::delete_) ||
           ((req.method() == boost::beast::http::verb::post) &&
            !checkPostUser(req));
}

void auditEvent(const crow::Request& req, const std::string& userName,
                bool success);

} // namespace audit
