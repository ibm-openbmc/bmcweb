#include "audit_events.hpp"

#include "http_request.hpp"
#include "logging.hpp"

#include <audit-records.h>
#include <audit_logging.h>

#include <boost/asio/ip/host_name.hpp>
#include <boost/beast/http/verb.hpp>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <string>

namespace audit
{

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static bool tryOpen = true;
static int auditfd = -1;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

int auditGetFD()
{
    return auditfd;
}

/**
 * @brief Closes connection for recording audit events
 */
void auditClose()
{
    if (auditfd >= 0)
    {
        audit_close(auditfd);
        auditfd = -1;
        BMCWEB_LOG_DEBUG("Audit log closed.");
    }
}

/**
 * @brief Opens connection for recording audit events
 *
 * Reuses prior connection if available.
 *
 * @return If connection was successful or not
 */
bool auditOpen()
{
    if (auditfd < 0)
    {
        /* Blocking opening of audit connection */
        if (!tryOpen)
        {
            BMCWEB_LOG_DEBUG("Audit connection disabled");
            return false;
        }

        auditfd = audit_open();

        if (auditfd < 0)
        {
            BMCWEB_LOG_ERROR("Error opening audit socket : {}", errno);
            return false;
        }
        BMCWEB_LOG_DEBUG("Audit fd created : {}", auditfd);
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
bool auditReopen()
{
    auditClose();
    return auditOpen();
}

/**
 * @brief Sets state for audit connection
 * @param[in] enable    New state for audit connection.
 *			If false, then any existing connection will be closed.
 */
void auditSetState(bool enable)
{
    if (!enable)
    {
        auditClose();
    }

    tryOpen = enable;

    BMCWEB_LOG_DEBUG("Audit state: tryOpen = {}", tryOpen);
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
    return req.target().starts_with("/redfish/v1/AccountService/Accounts") ||
           req.target().starts_with("/ibm/v1") ||
           ((req.method() == boost::beast::http::verb::post) &&
            checkPostUser(req));
}

/**
 * @brief Checks if request's detail data should be logged
 *
 * @return True if request's detail data should be logged
 */
bool wantDetail(const crow::Request& req)
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
            BMCWEB_LOG_DEBUG("Unexpected verb {}", req.methodString());
            return false;
    }
}

/**
 * @brief Appends item to strBuf only if strBuf won't exceed maxBufSize
 *
 * @param[in,out] strBuf Buffer to append up to maxBufSize only
 * @param[in] maxBufSize Maximum length of strBuf
 * @param[in] item String to append if it will fit within maxBufSize
 *
 * @return True if item was appended
 */
bool appendItemToBuf(std::string& strBuf, size_t maxBufSize,
                     const std::string& item)
{
    if (strBuf.length() + item.length() > maxBufSize)
    {
        return false;
    }
    strBuf += item;
    return true;
}

void auditEvent(const crow::Request& req, const std::string& userName,
                bool success)
{
    if (!auditOpen())
    {
        return;
    }

    std::string opPath =
        std::format("op={}:{} ", std::string(req.methodString()),
                    std::string(req.target()));

    size_t maxBuf = 256; // Limit message to avoid filling log with single entry
    std::string cnfgBuff = opPath.substr(0, maxBuf);

    if (cnfgBuff.length() < opPath.length())
    {
        // Event message truncated to fit into fixed sized buffer.
        BMCWEB_LOG_WARNING(
            "Audit buffer too small, truncating: cnfgBufLen={} opPathLen={}",
            cnfgBuff.length(), opPath.length());
    }

    // Determine any additional info for the event
    std::string detail;
    if (wantDetail(req))
    {
        detail = req.body().substr(0, maxBuf);
        detail += " ";
    }

    if (!detail.empty())
    {
        if (!appendItemToBuf(cnfgBuff, maxBuf, detail))
        {
            // Additional info won't fit into fixed sized buffer. Leave
            // it off.
            BMCWEB_LOG_WARNING(
                "Audit buffer too small for data: bufLeft={} detailLen={}",
                (maxBuf - cnfgBuff.length()), detail.length());
        }
    }

    // encode user account name to ensure it is in an appropriate format
    size_t userLen = 0;
    char* user = audit_encode_nv_string("acct", userName.c_str(), 0);

    if (user == nullptr)
    {
        BMCWEB_LOG_WARNING("Error encoding user for audit msg : {}", errno);
    }
    else
    {
        // setup a unique_ptr to handle freeing memory from user
        std::unique_ptr<char, void (*)(char*)> userUP(user, [](char* ptr) {
            if (ptr != nullptr)
            {
                // Linux audit mallocs memory we must free.
                // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
                ::free(ptr);
            }
        });

        userLen = std::strlen(user);

        if (!appendItemToBuf(cnfgBuff, maxBuf, std::string(user)))
        {
            // Username won't fit into fixed sized buffer. Leave it off.
            BMCWEB_LOG_WARNING(
                "Audit buffer too small for username: bufLeft={} userLen={}",
                (maxBuf - cnfgBuff.length()), userLen);
        }
    }

    BMCWEB_LOG_DEBUG(
        "auditEvent: bufLeft={}  opPathLen={} detailLen={} userLen={}",
        (maxBuf - cnfgBuff.length()), opPath.length(), detail.length(),
        userLen);

    std::string ipAddress = req.ipAddress.to_string();

    int rc = audit_log_user_message(
        auditfd, AUDIT_USYS_CONFIG, cnfgBuff.c_str(),
        boost::asio::ip::host_name().c_str(), ipAddress.c_str(), nullptr,
        int(success));

    if (rc <= 0)
    {
        // Something failed with existing connection. Try to establish a new
        // connection and retry if successful.
        // Preserve original errno to report if the retry fails.
        int origErrno = errno;
        if (auditReopen())
        {
            rc = audit_log_user_message(
                auditfd, AUDIT_USYS_CONFIG, cnfgBuff.c_str(),
                boost::asio::ip::host_name().c_str(), ipAddress.c_str(),
                nullptr, int(success));
        }
        if (rc <= 0)
        {
            BMCWEB_LOG_ERROR("Error writing audit message: {}", origErrno);
        }
    }
}

} // namespace audit
