#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "error_messages.hpp"
#include "generated/enums/log_service.hpp"
#include "http_body.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "http_utility.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/query_param.hpp"
#include "utils/time_utils.hpp"

#include <asm-generic/errno.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace redfish
{

/****************************************************
 * Redfish AuditLog interfaces
 ******************************************************/
inline void handleLogServicesAuditLogGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);

        return;
    }
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/system/LogServices/AuditLog";
    asyncResp->res.jsonValue["@odata.type"] = "#LogService.v1_2_0.LogService";
    asyncResp->res.jsonValue["Name"] = "Audit Log Service";
    asyncResp->res.jsonValue["Description"] = "Audit Log Service";
    asyncResp->res.jsonValue["Id"] = "AuditLog";
    asyncResp->res.jsonValue["OverWritePolicy"] =
        log_service::OverWritePolicy::WrapsWhenFull;
    asyncResp->res.jsonValue["Entries"]["@odata.id"] =
        "/redfish/v1/Systems/system/LogServices/AuditLog/Entries";

    std::pair<std::string, std::string> redfishDateTimeOffset =
        redfish::time_utils::getDateTimeOffsetNow();
    asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
    asyncResp->res.jsonValue["DateTimeLocalOffset"] =
        redfishDateTimeOffset.second;
}

enum class AuditLogParseError
{
    success,
    parseFailed,
    messageIdNotInRegistry,
};

static AuditLogParseError fillAuditLogEntryJson(
    const nlohmann::json& auditEntry, nlohmann::json::object_t& logEntryJson)
{
    if (auditEntry.is_discarded())
    {
        return AuditLogParseError::parseFailed;
    }

    std::string logEntryID;
    std::string entryTimeStr;
    const std::string& messageID = "OpenBMC.0.5.AuditLogUsysConfig";
    nlohmann::json messageArgs = nlohmann::json::array();
    std::map<std::string, uint>::const_iterator mapEntry;
    const std::map<std::string, uint> msgArgMap(
        {{"Type", 0},
         {"Operation", 1},
         {"Account", 2},
         {"Executable", 3},
         {"Hostname", 4},
         {"IPAddress", 5},
         {"Terminal", 6},
         {"Result", 7}});
    for (const auto& item : auditEntry.items())
    {
        if (item.key() == "ID")
        {
            logEntryID = item.value();
        }
        else if (item.key() == "EventTimestamp")
        {
            uint64_t timestamp = item.value();
            entryTimeStr = redfish::time_utils::getDateTimeUint(timestamp);
        }
        else
        {
            /* The rest of the properties either go into the MessageArgs or
             * they are not part of the response.
             */
            mapEntry = msgArgMap.find(item.key());
            if (mapEntry != msgArgMap.end())
            {
                messageArgs[mapEntry->second] = item.value();
            }
        }
    }

    // Check that we found all of the expected fields.
    if (logEntryID.empty())
    {
        BMCWEB_LOG_ERROR("Missing ID");
        return AuditLogParseError::parseFailed;
    }

    if (entryTimeStr.empty())
    {
        BMCWEB_LOG_ERROR("Missing Timestamp");
        return AuditLogParseError::parseFailed;
    }

    // Get the Message from the MessageRegistry
    const registries::Message* message = registries::getMessage(messageID);

    if (message == nullptr)
    {
        BMCWEB_LOG_WARNING("Log entry not found in registry: {} ", messageID);
        return AuditLogParseError::messageIdNotInRegistry;
    }

    std::string msg = message->message;

    // Fill the MessageArgs into the Message
    if (!messageArgs.empty())
    {
        if (messageArgs[0] != "USYS_CONFIG")
        {
            BMCWEB_LOG_WARNING("Unexpected audit log entry type: {}",
                               messageArgs[0]);
        }

        uint i = 0;
        for (auto messageArg : messageArgs)
        {
            if (messageArg == nullptr)
            {
                BMCWEB_LOG_DEBUG("Handle null messageArg: {}", i);
                messageArg = "";
                messageArgs[i] = "";
            }

            std::string argStr = "%" + std::to_string(++i);
            size_t argPos = msg.find(argStr);
            if (argPos != std::string::npos)
            {
                msg.replace(argPos, argStr.length(), messageArg);
            }
        }
    }

    // Fill in the log entry with the gathered data
    logEntryJson["@odata.type"] = "#LogEntry.v1_9_0.LogEntry";
    logEntryJson["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/system/LogServices/AuditLog/Entries/{}",
        logEntryID);
    logEntryJson["Name"] = "Audit Log Entry";
    logEntryJson["Id"] = logEntryID;
    logEntryJson["MessageId"] = messageID;
    logEntryJson["Message"] = std::move(msg);
    logEntryJson["MessageArgs"] = std::move(messageArgs);
    logEntryJson["EntryType"] = log_service::LogEntryTypes::Event;
    logEntryJson["EventTimestamp"] = std::move(entryTimeStr);

    /* logEntryJson["Oem"]["IBM"]["@odata.id"] ?? */
    logEntryJson["Oem"]["IBM"]["@odata.type"] =
        "#IBMLogEntryAttachment.v1_0_0.IBM";
    logEntryJson["Oem"]["IBM"]["AdditionalDataFullAuditLogURI"] =
        "/redfish/v1/Systems/system/LogServices/AuditLog/Entries/FullAudit/attachment";

    return AuditLogParseError::success;
}

/**
 * @brief Read single line from POSIX stream
 * @details Maximum length of line read is 4096 characters. Longer lines than
 *          this will be truncated and a warning is logged. This is to guard
 *          against malformed data using unexpected amounts of memory.
 *
 * @param[in] logStream - POSIX stream
 * @param[out] logLine - Filled in with line read on success
 *
 * @return True if line was read even if truncated. False if EOF reached or
 *         error reading from the logStream.
 */
inline bool readLine(FILE* logStream, std::string& logLine)
{
    std::array<char, 4096> buffer{};

    if (fgets(buffer.data(), buffer.size(), logStream) == nullptr)
    {
        /* Failed to read, could be EOF.
         * Report error if not EOF.
         */
        if (feof(logStream) == 0)
        {
            BMCWEB_LOG_ERROR("Failure reading logStream: {}", errno);
        }
        return false;
    }

    logLine.assign(buffer.data());

    if (!logLine.ends_with('\n'))
    {
        /* Line was too long for the buffer.
         * Could repeat reads or increase size of buffer.
         * Don't expect log lines to be longer than 4096 so
         * read to get to end of this line and discard rest of the line.
         * Return just the part of the line which fit in the original
         * buffer and let the caller handle it.
         */
        std::array<char, 4096> skipBuf{};
        std::string skipLine;
        auto totalLength = logLine.size();

        do
        {
            if (fgets(skipBuf.data(), skipBuf.size(), logStream) != nullptr)
            {
                skipLine.assign(skipBuf.data());
                totalLength += skipLine.size();
            }

            if (ferror(logStream) != 0)
            {
                BMCWEB_LOG_ERROR("Failure reading logStream: {}", errno);
                break;
            }

            if (feof(logStream) != 0)
            {
                /* Reached EOF trying to find the end of this line. */
                break;
            }
        } while (!skipLine.ends_with('\n'));

        BMCWEB_LOG_WARNING(
            "Line too long for logStream, line is truncated. Line length: {}",
            totalLength);
    }

    /* Return success even if line was truncated */
    return true;
}

/**
 * @brief Reads the audit log entries and writes them to Members array
 *
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] unixfd - File descriptor for Audit Log file
 * @param[in] skip - Query paramater skip value
 * @param[in] top - Query paramater top value
 */
inline void readAuditLogEntries(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::unix_fd& unixfd, size_t skip, size_t top)
{
    auto fd = dup(unixfd);
    if (fd == -1)
    {
        BMCWEB_LOG_ERROR("Failed to duplicate fd {}", static_cast<int>(unixfd));
        messages::internalError(asyncResp->res);
        return;
    }

    FILE* logStream = fdopen(fd, "r");
    if (logStream == nullptr)
    {
        BMCWEB_LOG_ERROR("Failed to open fd {}", fd);
        messages::internalError(asyncResp->res);
        close(fd);
        return;
    }

    nlohmann::json& logEntryArray = asyncResp->res.jsonValue["Members"];
    if (logEntryArray.empty())
    {
        logEntryArray = nlohmann::json::array();
    }

    uint64_t entryCount = 0;
    std::string logLine;
    while (readLine(logStream, logLine))
    {
        /* Note: entryCount counts all entries even ones with parse errors.
         *       This allows the top/skip semantics to work correctly and a
         *       consistent count to be returned.
         */
        entryCount++;
        BMCWEB_LOG_DEBUG("{}:logLine: {}", entryCount, logLine);

        /* Handle paging using skip (number of entries to skip from the
         * start) and top (number of entries to display).
         * Don't waste cycles parsing if we are going to skip sending this entry
         */
        if (entryCount <= skip || entryCount > skip + top)
        {
            BMCWEB_LOG_DEBUG("Query param skips, line={}", entryCount);
            continue;
        }

        nlohmann::json::object_t bmcLogEntry;

        auto auditEntry = nlohmann::json::parse(logLine, nullptr, false);

        AuditLogParseError status =
            fillAuditLogEntryJson(auditEntry, bmcLogEntry);
        if (status != AuditLogParseError::success)
        {
            BMCWEB_LOG_ERROR("Failed to parse line={}", entryCount);
            messages::internalError(asyncResp->res);
            continue;
        }

        logEntryArray.push_back(std::move(bmcLogEntry));
    }

    asyncResp->res.jsonValue["Members@odata.count"] = entryCount;

    if (skip + top < entryCount)
    {
        asyncResp->res
            .jsonValue["Members@odata.nextLink"] = boost::urls::format(
            "/redfish/v1/Systems/system/LogServices/AuditLog/Entries?$skip={}",
            std::to_string(skip + top));
    }

    /* Not writing to file, so can safely ignore error on close */
    (void)fclose(logStream);
}

/**
 * @brief Retrieves the targetID entry from the unixfd
 *
 * @param[in] asyncResp - The redfish response to return.
 * @param[in] unixfd - File descriptor for Audit Log file
 * @param[in] targetID - ID of entry to retrieve
 */
inline void getAuditLogEntryByID(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::unix_fd& unixfd, const std::string& targetID)
{
    bool found = false;

    auto fd = dup(unixfd);
    if (fd == -1)
    {
        BMCWEB_LOG_ERROR("Failed to duplicate fd {}", static_cast<int>(unixfd));
        messages::internalError(asyncResp->res);
        return;
    }

    FILE* logStream = fdopen(fd, "r");
    if (logStream == nullptr)
    {
        BMCWEB_LOG_ERROR("Failed to open fd {}", fd);
        messages::internalError(asyncResp->res);
        close(fd);
        return;
    }

    uint64_t entryCount = 0;
    std::string logLine;
    while (readLine(logStream, logLine))
    {
        entryCount++;
        BMCWEB_LOG_DEBUG("{}:logLine: {}", entryCount, logLine);

        auto auditEntry = nlohmann::json::parse(logLine, nullptr, false);
        auto idIt = auditEntry.find("ID");
        if (idIt != auditEntry.end() && *idIt == targetID)
        {
            found = true;
            nlohmann::json::object_t bmcLogEntry;
            AuditLogParseError status =
                fillAuditLogEntryJson(auditEntry, bmcLogEntry);
            if (status != AuditLogParseError::success)
            {
                BMCWEB_LOG_ERROR("Failed to parse line={}", entryCount);
                messages::internalError(asyncResp->res);
            }
            else
            {
                asyncResp->res.jsonValue.update(bmcLogEntry);
            }
            break;
        }
    }

    if (!found)
    {
        messages::resourceNotFound(asyncResp->res, "LogEntry", targetID);
    }

    /* Not writing to file, so can safely ignore error on close */
    (void)fclose(logStream);
}

inline void handleLogServicesAuditLogEntriesCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    query_param::QueryCapabilities capabilities = {
        .canDelegateTop = true,
        .canDelegateSkip = true,
    };
    query_param::Query delegatedQuery;
    if (!redfish::setUpRedfishRouteWithDelegation(app, req, asyncResp,
                                                  delegatedQuery, capabilities))
    {
        return;
    }

    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);

        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#LogEntryCollection.LogEntryCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/system/LogServices/AuditLog/Entries";
    asyncResp->res.jsonValue["Name"] = "Audit Log Entries";
    asyncResp->res.jsonValue["Description"] = "Collection of Audit Log Entries";
    size_t skip = delegatedQuery.skip.value_or(0);
    size_t top = delegatedQuery.top.value_or(query_param::Query::maxTop);

    /* Create unique entry for each entry in log file.
     */
    crow::connections::systemBus->async_method_call(
        [asyncResp, skip, top](const boost::system::error_code& ec,
                               const sdbusplus::message::unix_fd& unixfd) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("AuditLog resp_handler got error {}",
                                 ec.value());
                messages::internalError(asyncResp->res);
                return;
            }

            readAuditLogEntries(asyncResp, unixfd, skip, top);
        },
        "xyz.openbmc_project.Logging.AuditLog",
        "/xyz/openbmc_project/logging/auditlog",
        "xyz.openbmc_project.Logging.AuditLog", "GetLatestEntries", top);
}

inline void handleLogServicesAuditLogEntryGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& targetID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    /* Search for entry matching targetID. */
    crow::connections::systemBus->async_method_call(
        [asyncResp, targetID](const boost::system::error_code& ec,
                              const sdbusplus::message::unix_fd& unixfd) {
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    messages::resourceNotFound(asyncResp->res, "AuditLog",
                                               targetID);
                    return;
                }
                BMCWEB_LOG_ERROR("AuditLog resp_handler got error {}",
                                 ec.value());
                messages::internalError(asyncResp->res);
                return;
            }

            getAuditLogEntryByID(asyncResp, unixfd, targetID);
        },
        "xyz.openbmc_project.Logging.AuditLog",
        "/xyz/openbmc_project/logging/auditlog",
        "xyz.openbmc_project.Logging.AuditLog", "GetLatestEntries",
        query_param::Query::maxTop);
}

inline void getFullAuditLogAttachment(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::unix_fd& unixfd)
{
    int fd = -1;
    fd = dup(unixfd);
    if (fd == -1)
    {
        BMCWEB_LOG_ERROR("Failed to duplicate fd {}", static_cast<int>(unixfd));
        messages::internalError(asyncResp->res);
        return;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size == -1)
    {
        BMCWEB_LOG_ERROR("Failed to get size of fd {}", fd);
        messages::internalError(asyncResp->res);
        close(fd);
        return;
    }
    /* Reset seek pointer so download will start at beginning */
    off_t rc = lseek(fd, 0, SEEK_SET);
    if (rc == -1)
    {
        BMCWEB_LOG_ERROR("Failed to reset file offset to 0");
        messages::internalError(asyncResp->res);
        close(fd);
        return;
    }

    /* Max file size based on default configuration:
     *   - Raw audit log: 10MB
     *   - Allow up to 20MB to adjust for JSON metadata
     */
    constexpr int maxFileSize = 20971520;
    if (size > maxFileSize)
    {
        BMCWEB_LOG_ERROR("File size {} exceeds maximum allowed size of {}",
                         size, maxFileSize);
        messages::internalError(asyncResp->res);
        close(fd);
        return;
    }

    if (!asyncResp->res.openFd(fd, bmcweb::EncodingType::Base64))
    {
        BMCWEB_LOG_ERROR("Failed to open fd {} for response", fd);
        messages::internalError(asyncResp->res);
        close(fd);
        return;
    }

    asyncResp->res.addHeader(boost::beast::http::field::content_type,
                             "application/octet-stream");
    asyncResp->res.addHeader(
        boost::beast::http::field::content_transfer_encoding, "Base64");
}

inline void handleFullAuditLogAttachment(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& entryID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_DEBUG("Route setup failed");
        return;
    }

    if (!http_helpers::isContentTypeAllowed(
            req.getHeaderValue("Accept"),
            http_helpers::ContentType::OctetStream, true))
    {
        BMCWEB_LOG_ERROR("Content type not allowed");
        asyncResp->res.result(boost::beast::http::status::bad_request);
        return;
    }
    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (entryID != "FullAudit")
    {
        messages::resourceNotFound(asyncResp->res, "ID", entryID);
        return;
    }

    /* Download attachment */
    crow::connections::systemBus->async_method_call(
        [asyncResp, entryID](const boost::system::error_code& ec,
                             const sdbusplus::message::unix_fd& unixfd) {
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    messages::resourceNotFound(asyncResp->res, "AuditLog",
                                               entryID);
                    return;
                }
                BMCWEB_LOG_ERROR("AuditLog resp_handler got error {}",
                                 ec.value());
                messages::internalError(asyncResp->res);
                return;
            }

            getFullAuditLogAttachment(asyncResp, unixfd);
        },
        "xyz.openbmc_project.Logging.AuditLog",
        "/xyz/openbmc_project/logging/auditlog",
        "xyz.openbmc_project.Logging.AuditLog", "GetAuditLog");
}

inline void requestRoutesLogServicesAudit(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/AuditLog/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLogServicesAuditLogGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/AuditLog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesAuditLogEntriesCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/AuditLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLogServicesAuditLogEntryGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/AuditLog/Entries/<str>/attachment")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleFullAuditLogAttachment, std::ref(app)));
}

} // namespace redfish
