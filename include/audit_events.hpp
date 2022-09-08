#pragma once

#ifdef BMCWEB_ENABLE_LINUX_AUDIT_EVENTS
#include <libaudit.h>

#include <boost/asio/ip/host_name.hpp>
#endif

namespace audit
{

void auditAcctEvent([[maybe_unused]] int type,
                    [[maybe_unused]] const char* username,
                    [[maybe_unused]] uid_t uid,
                    [[maybe_unused]] const char* remoteHostName,
                    [[maybe_unused]] const char* remoteIpAddress,
                    [[maybe_unused]] const char* tty,
                    [[maybe_unused]] bool success)
{
#ifdef BMCWEB_ENABLE_LINUX_AUDIT_EVENTS
    int auditfd;

    const char* op = NULL;

    auditfd = audit_open();

    if (auditfd < 0)
    {
        BMCWEB_LOG_ERROR << "Error opening audit socket : " << strerror(errno);
        return;
    }
    if (type == AUDIT_USER_LOGIN)
    {
        op = "login";
    }
    else if (type == AUDIT_USER_LOGOUT)
    {
        op = "logout";
    }

    if (audit_log_acct_message(auditfd, type, NULL, op, username, uid,
                               remoteHostName, remoteIpAddress, tty,
                               int(success)) <= 0)
    {
        BMCWEB_LOG_ERROR << "Error writing audit message: " << strerror(errno);
    }

    close(auditfd);
#endif
    return;
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
