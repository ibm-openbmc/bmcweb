#pragma once

#ifdef BMCWEB_ENABLE_LINUX_AUDIT_EVENTS
#include <libaudit.h>

#include <boost/asio/ip/host_name.hpp>
#endif

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

inline void auditEvent([[maybe_unused]] const crow::Request& req,
                       [[maybe_unused]] const char* opPath,
                       [[maybe_unused]] bool success)
{
#ifdef BMCWEB_ENABLE_LINUX_AUDIT_EVENTS
    int auditfd;

    char cnfgBuff[256];
    char* user = NULL;

    auditfd = audit_open();

    if (auditfd < 0)
    {
        BMCWEB_LOG_ERROR << "Error opening audit socket : " << strerror(errno);
        return;
    }

    strcpy(cnfgBuff, opPath);

    // encode user account name to ensure it is in an appropriate format
    user = audit_encode_nv_string("acct", req.session->username.c_str(), 0);
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
                               req.ipAddress.to_string().c_str(), NULL,
                               int(success)) <= 0)
    {
        BMCWEB_LOG_ERROR << "Error writing audit message: " << strerror(errno);
    }

    close(auditfd);
#endif
    return;
}

} // namespace audit
