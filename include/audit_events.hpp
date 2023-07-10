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
 * @brief Checks if POST request is a user connection event
 *
 * Login and Session requests are audited when the authentication is attempted.
 * This allows failed requests to be audited with the user detail.
 *
 * @return True if request is a user connection event
 */
inline bool checkPostUser(const crow::Request& req)
{
    if ((req.target() == "/redfish/v1/SessionService/Sessions") ||
        (req.target() == "/redfish/v1/SessionService/Sessions/") ||
        (req.target() == "/login"))
    {
        return true;
    }
    return false;
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
         !checkPostUser(req)))
    {
        return true;
    }

    return false;
}

/**
 * @brief Checks if request should include additional data
 *
 * - Accounts requests data may contain passwords
 * - IBM Management console events data is not useful. It can be binary data or
 *   contents of file.
 * - User login and session data may contain passwords
 *
 * @return True if request's data should not be logged
 */
inline bool checkSkipDetail(const crow::Request& req)
{
    if (req.target().starts_with("/redfish/v1/AccountService/Accounts") ||
        req.target().starts_with("/ibm/v1") ||
        ((req.method() == boost::beast::http::verb::post) &&
         checkPostUser(req)))
    {
        return true;
    }
    return false;
}

/**
 * @brief Checks if request's detail data should be logged
 *
 * @return True if request's detail data should be logged
 */
inline bool wantDetail(const crow::Request& req)
{
    switch (req.method())
    {
        case boost::beast::http::verb::patch:
        case boost::beast::http::verb::post:
            if (checkSkipDetail(req))
            {
                return false;
            }
            return true;

        case boost::beast::http::verb::put:
            return (!req.target().starts_with("/ibm/v1"));

        case boost::beast::http::verb::delete_:
            return true;

        default:
            // Shouldn't be here, don't log any data
            BMCWEB_LOG_DEBUG << "Unexpected verb " << req.method();
            return false;
    }
}

inline void auditEvent(const crow::Request& req, const std::string& userName,
                       bool success)
{
    std::string opPath;
    char cnfgBuff[256];
    size_t bufLeft = sizeof(cnfgBuff); // Amount left available in cnfgBuff
    int rc;
    int origErrno;
    std::string ipAddress;
    std::string detail;

    if (!auditOpen())
    {
        return;
    }

    opPath = "op=" + std::string(req.methodString()) + ":" +
             std::string(req.target()) + " ";
    // Account for NULL
    size_t opPathLen = opPath.length() + 1;
    if (opPathLen > bufLeft)
    {
        // Truncate event message to fit into fixed sized buffer.
        BMCWEB_LOG_WARNING << "Audit buffer too small, truncating:"
                           << " bufLeft=" << bufLeft
                           << " opPathLen=" << opPathLen;
        opPathLen = bufLeft;
    }
    strncpy(cnfgBuff, opPath.c_str(), opPathLen);
    cnfgBuff[opPathLen - 1] = '\0';
    bufLeft -= opPathLen;

    // Determine any additional info for the event
    if (wantDetail(req))
    {
        detail = req.body + " ";
    }

    if (!detail.empty())
    {
        if (detail.length() > bufLeft)
        {
            // Additional info won't fix into fixed sized buffer. Leave
            // it off.
            BMCWEB_LOG_WARNING << "Audit buffer too small for data:"
                               << " bufLeft=" << bufLeft
                               << " detailLen=" << detail.length();
        }
        else
        {
            strncat(cnfgBuff, detail.c_str(), detail.length());
            bufLeft -= detail.length();
        }
    }

    // encode user account name to ensure it is in an appropriate format
    size_t userLen = 0;
    char* user = audit_encode_nv_string("acct", userName.c_str(), 0);

    // setup a unique_ptr to handle freeing memory from user
    std::unique_ptr<char, void (*)(char*)> userUP(user, [](char* ptr) {
        if (ptr != nullptr)
        {
            ::free(ptr);
        }
    });

    if (user == NULL)
    {
        BMCWEB_LOG_ERROR << "Error appending user to audit msg : " << errno;
    }
    else
    {
        userLen = std::strlen(user);

        if (userLen > bufLeft)
        {
            // Username won't fit into fixed sized buffer. Leave it off.
            BMCWEB_LOG_WARNING << "Audit buffer too small for username:"
                               << " bufLeft=" << bufLeft
                               << " userLen=" << userLen;
        }
        else
        {
            strncat(cnfgBuff, user, userLen);
            bufLeft -= userLen;
        }
    }

    BMCWEB_LOG_DEBUG << "auditEvent: bufLeft=" << bufLeft
                     << " opPathLen=" << opPathLen
                     << " detailLen=" << detail.length()
                     << " userLen=" << userLen;

    ipAddress = req.ipAddress.to_string();

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
