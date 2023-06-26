#pragma once

#include "http_request.hpp"
#include "logging.hpp"

#include <libaudit.h>

#include <boost/asio/ip/host_name.hpp>

#include <cstring>
#include <string>

namespace audit
{

bool tryOpen = true;
int auditfd = -1;

/**
 * @brief Closes connection for recording audit events
 */
inline void auditClose(void)
{
    if (auditfd >= 0)
    {
        audit_close(auditfd);
        auditfd = -1;
        BMCWEB_LOG_DEBUG << "Audit log closed.";
    }

    return;
}

/**
 * @brief Opens connection for recording audit events
 *
 * Reuses prior connection if available.
 *
 * @return If connection was successful or not
 */
inline bool auditOpen(void)
{
    if (auditfd < 0)
    {
        /* Blocking opening of audit connection */
        if (!tryOpen)
        {
            BMCWEB_LOG_DEBUG << "Audit connection disabled";
            return false;
        }

        auditfd = audit_open();

        if (auditfd < 0)
        {
            BMCWEB_LOG_ERROR << "Error opening audit socket : " << errno;
            return false;
        }
        BMCWEB_LOG_DEBUG << "Audit fd created : " << auditfd;
    }

    return true;
}

/**
 * @brief Establishes new connection for recording audit events
 *
 * Closes any existing connection and tries to create a new connection.
 *
 * @return If new connection was successful or not
 */
inline bool auditReopen(void)
{
    auditClose();
    return auditOpen();
}

/**
 * @brief Sets state for audit connection
 * @param[in] enable    New state for audit connection.
 *			If false, then any existing connection will be closed.
 */
inline void auditSetState(bool enable)
{
    if (enable == false)
    {
        auditClose();
    }

    tryOpen = enable;

    BMCWEB_LOG_DEBUG << "Audit state: tryOpen = " << tryOpen;

    return;
}

/**
 * @brief Checks if POST request should be audited on completion
 *
 * Login and Session requests are audited when the authentication is attempted.
 * This allows failed requests to be audited with the user detail.
 *
 * @return True if request should be audited
 */
inline bool checkPostAudit(const crow::Request& req)
{
    if ((req.target() == "/redfish/v1/SessionService/Sessions") ||
        (req.target() == "/redfish/v1/SessionService/Sessions/") ||
        (req.target() == "/login"))
    {
        return false;
    }
    return true;
}

/**
 * @brief Checks if request should be audited after completion
 * @return  True if request should be audited
 */
inline bool wantAudit(const crow::Request& req)
{
    if ((req.method() == boost::beast::http::verb::patch) ||
        (req.method() == boost::beast::http::verb::put) ||
        (req.method() == boost::beast::http::verb::delete_) ||
        ((req.method() == boost::beast::http::verb::post) &&
         checkPostAudit(req)))
    {
        return true;
    }

    return false;
}

inline void auditEvent(const char* opPath, const std::string& userName,
                       const std::string& ipAddress, bool success)
{
    int code = __LINE__;

    char cnfgBuff[256];
    size_t bufLeft = 256; // Amount left available in cnfgBuff
    char* user = NULL;
    size_t opPathLen;
    size_t userLen = 0;
    int rc;
    int origErrno;

    if (auditOpen() == false)
    {
        return;
    }

    opPathLen = std::strlen(opPath) + 1;
    if (opPathLen > bufLeft)
    {
        // Truncate event message to fit into fixed sized buffer.
        BMCWEB_LOG_WARNING << "Audit buffer too small, truncating:"
                           << " bufLeft=" << bufLeft
                           << " opPathLen=" << opPathLen;
        opPathLen = bufLeft;
        code = __LINE__;
    }
    strncpy(cnfgBuff, opPath, opPathLen);
    cnfgBuff[opPathLen - 1] = '\0';
    bufLeft -= opPathLen;

    // encode user account name to ensure it is in an appropriate format
    user = audit_encode_nv_string("acct", userName.c_str(), 0);
    if (user == NULL)
    {
        BMCWEB_LOG_ERROR << "Error appending user to audit msg : " << errno;
        code = __LINE__;
    }
    else
    {
        userLen = std::strlen(user) + 1;

        if (userLen > bufLeft)
        {
            // Username won't fit into fixed sized buffer. Leave it off.
            BMCWEB_LOG_WARNING << "Audit buffer too small for username:"
                               << " bufLeft=" << bufLeft
                               << " userLen=" << userLen;
            code = __LINE__;
        }
        else
        {
            strncat(cnfgBuff, user, userLen);
        }
    }

    BMCWEB_LOG_DEBUG << "auditEvent: code=" << code << " bufLeft=" << bufLeft
                     << " opPathLen=" << opPathLen << " userLen=" << userLen;

    free(user);

    rc = audit_log_user_message(auditfd, AUDIT_USYS_CONFIG, cnfgBuff,
                                boost::asio::ip::host_name().c_str(),
                                ipAddress.c_str(), NULL, int(success));

    if (rc <= 0)
    {
        // Something failed with existing connection. Try to establish a new
        // connection and retry if successful.
        // Preserve original errno to report if the retry fails.
        origErrno = errno;
        if (auditReopen())
        {
            rc = audit_log_user_message(auditfd, AUDIT_USYS_CONFIG, cnfgBuff,
                                        boost::asio::ip::host_name().c_str(),
                                        ipAddress.c_str(), NULL, int(success));
        }
        if (rc <= 0)
        {
            BMCWEB_LOG_ERROR << "Error writing audit message: " << origErrno;
        }
    }

    return;
}

} // namespace audit
