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

    char cnfgBuff[256];
    char* user = NULL;

    auditfd = audit_open();

    if (auditfd < 0)
    {
        BMCWEB_LOG_ERROR << "Error opening audit socket : " << strerror(errno);
        return;
    }

    strncpy(cnfgBuff, opPath, std::strlen(opPath) + 1);

    // encode user account name to ensure it is in an appropriate format
    user = audit_encode_nv_string("acct", userName.c_str(), 0);
    if (user == NULL)
    {
        BMCWEB_LOG_ERROR << "Error appending user to audit msg : "
                         << strerror(errno);
    }
    else
    {
        strncat(cnfgBuff, user, 50);
    }

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
