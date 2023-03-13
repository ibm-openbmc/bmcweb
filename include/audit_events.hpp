#pragma once

#include <libaudit.h>

#include <boost/asio/ip/host_name.hpp>

#include <cstring>
#include <string>

namespace audit
{

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

inline void auditEvent(const char* opPath, const std::string& userName,
                       const std::string& ipAddress, bool success)
{
    int auditfd;
    int code = __LINE__;

    char cnfgBuff[256];
    size_t bufLeft = 256; // Amount left available in cnfgBuff
    char* user = NULL;
    size_t opPathLen;
    size_t userLen = 0;

    auditfd = audit_open();

    if (auditfd < 0)
    {
        BMCWEB_LOG_ERROR << "Error opening audit socket : " << strerror(errno);
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
        BMCWEB_LOG_ERROR << "Error appending user to audit msg : "
                         << strerror(errno);
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

    if (audit_log_user_message(auditfd, AUDIT_USYS_CONFIG, cnfgBuff,
                               boost::asio::ip::host_name().c_str(),
                               ipAddress.c_str(), NULL, int(success)) <= 0)
    {
        BMCWEB_LOG_ERROR << "Error writing audit message: " << strerror(errno);
    }

    close(auditfd);
    return;
}

} // namespace audit
