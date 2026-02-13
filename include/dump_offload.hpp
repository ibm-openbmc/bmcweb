#pragma once

#include "app.hpp"
// NOLINTBEGIN(misc-include-cleaner)
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "http_stream.hpp"
#include "ibm_utils.hpp"
#include "io_context_singleton.hpp"
#include "logging.hpp"
#include "privileges.hpp"
#include "registries/privilege_registry.hpp"

#include <asm-generic/errno.h>
#include <sys/select.h>

#include <boost/asio.hpp>
#include <boost/asio/basic_socket_acceptor.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast/core/flat_static_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/property.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
// NOLINTEND(misc-include-cleaner)

namespace crow
{
namespace obmc_dump
{

static constexpr std::string_view unixSocketPathDir =
    "/tmp/DumpOffloadSockets/";

inline void handleDumpOffloadUrl(const crow::Request& req, crow::Response& res,
                                 const std::string& entryId,
                                 const std::string& dumpEntryType);
inline void resetHandler();

static constexpr size_t socketBufferSize = static_cast<size_t>(64 * 1024);
static constexpr uint8_t maxConnectRetryCount = 3;

/** class Handler
 *  Handles data transfer between unix domain socket and http connection socket.
 *  This handler invokes dump offload reads data from unix domain socket
 *  and writes on to http stream connection socket.
 */
class Handler : public std::enable_shared_from_this<Handler>
{
  public:
    Handler(boost::asio::io_context& ios, const std::string& entryIDIn,
            const std::string& dumpTypeIn,
            const std::string& unixSocketPathIn) :
        entryID(entryIDIn), dumpType(dumpTypeIn),
        unixSocketPath(unixSocketPathIn), unixSocket(ios), waitTimer(ios)
    {}
    ~Handler()
    {
        BMCWEB_LOG_DEBUG("~Handler callled");
    }

    Handler(const Handler&) = delete;
    Handler(Handler&&) = delete;
    Handler& operator=(const Handler&) = delete;
    Handler& operator=(Handler&&) = delete;

    /**
     * @brief Connects to unix socket to read dump data
     *
     * @return void
     */
    void doConnect()
    {
        unixSocket.async_connect(
            unixSocketPath.c_str(), [this, self(weak_from_this())](
                                        const boost::system::error_code& ec) {
                auto sharedSelf = self.lock();
                if (!sharedSelf)
                {
                    return;
                }
                if (ec)
                {
                    // TODO:
                    // right now we don't have dbus method which can make sure
                    // unix socket is ready to accept connection so its possible
                    // that bmcweb can try to connect to socket before even
                    // socket setup, so using retry mechanism with timeout.
                    if (ec == boost::system::errc::no_such_file_or_directory ||
                        ec == boost::system::errc::connection_refused)
                    {
                        BMCWEB_LOG_DEBUG("UNIX Socket: async_connect {}",
                                         ec.message());
                        retrySocketConnect();
                        return;
                    }
                    BMCWEB_LOG_ERROR("UNIX Socket: async_connect error {}",
                                     ec.message());
                    waitTimer.cancel();
                    this->connection->sendStreamErrorStatus(
                        boost::beast::http::status::internal_server_error);
                    this->connection->close();
                    this->cleanupSocketFiles();
                    return;
                }
                waitTimer.cancel();
                this->connection->sendStreamHeaders(
                    std::to_string(this->dumpSize), "application/octet-stream");
                this->doReadStream();
            });
    }

    /**
     * @brief  Invokes InitiateOffload method of dump manager which
     *         directs dump manager to start writing on unix domain socket.
     *
     * @return void
     */
    void initiateOffload()
    {
        std::string dumpEntryPath = std::format(
            "/xyz/openbmc_project/dump/{}/entry/{}", dumpType, entryID);
        dbus::utility::async_method_call(
            [this,
             self(weak_from_this())](const boost::system::error_code& ec) {
                auto sharedSelf = self.lock();
                if (!sharedSelf)
                {
                    return;
                }
                if (ec)
                {
                    if (ec.value() == EBADR)
                    {
                        this->connection->sendStreamErrorStatus(
                            boost::beast::http::status::not_found);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("DBUS response error: {}",
                                         ec.message());
                        this->connection->sendStreamErrorStatus(
                            boost::beast::http::status::internal_server_error);
                    }
                    this->connection->close();
                    this->cleanupSocketFiles();
                    return;
                }
            },
            "xyz.openbmc_project.Dump.Manager", dumpEntryPath,
            "xyz.openbmc_project.Dump.Entry", "InitiateOffload",
            unixSocketPath.c_str());
    }

    /**
     * @brief  This function setup a timer for retrying unix socket connect.
     *
     * @return void
     */
    void retrySocketConnect()
    {
        waitTimer.expires_after(std::chrono::milliseconds(1000));

        waitTimer.async_wait([this, self(weak_from_this())](
                                 const boost::system::error_code& ec) {
            auto sharedSelf = self.lock();
            if (!sharedSelf)
            {
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR("Async_wait failed {}", ec.message());
                return;
            }

            if (connectRetryCount < maxConnectRetryCount)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to connect, retrying... Retry count: {}",
                    connectRetryCount);
                connectRetryCount++;
                doConnect();
            }
            else
            {
                BMCWEB_LOG_ERROR("Failed to connect, reached max retry count: ",
                                 connectRetryCount);
                waitTimer.cancel();
                this->cleanupSocketFiles();
                this->connection->setStreamHeaders("Retry-After", "60");
                this->connection->sendStreamErrorStatus(
                    boost::beast::http::status::service_unavailable);
                this->connection->close();
                return;
            }
        });
    }

    void resetOffloadURI()
    {
        BMCWEB_LOG_DEBUG("resetOffloadURI");
        std::string dumpEntryPath = std::format(
            "/xyz/openbmc_project/dump/{}/entry/{}", dumpType, entryID);
        std::string value;
        sdbusplus::asio::setProperty(
            *crow::connections::systemBus, "xyz.openbmc_project.Dump.Manager",
            dumpEntryPath, "xyz.openbmc_project.Dump.Entry", "OffloadUri",
            std::variant<std::string>(value),
            [this,
             self(weak_from_this())](const boost::system::error_code& ec) {
                auto sharedSelf = self.lock();
                if (!sharedSelf)
                {
                    return;
                }
                if (ec)
                {
                    if (ec.value() == EBADR)
                    {
                        this->connection->sendStreamErrorStatus(
                            boost::beast::http::status::not_found);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error: Unable to set the dump OffloadUri {}",
                            ec);
                        this->connection->sendStreamErrorStatus(
                            boost::beast::http::status::internal_server_error);
                    }
                    return;
                }
                BMCWEB_LOG_INFO("Reset OffloadUri of {} dump id {}", dumpType,
                                entryID);
            });
    }

    void cancelAsyncOperations()
    {
        // Cancel the wait timer to release any pending callbacks
        waitTimer.cancel();

        // Close the socket to cancel any pending async operations
        boost::system::error_code ec;
        if (unixSocket.is_open())
        {
            unixSocket.close(ec);
        }
    }

    void cleanupSocketFiles()
    {
        if (unixSocket.is_open())
        {
            unixSocket.close();
        }
        std::error_code ec;
        bool fileExists = std::filesystem::exists(unixSocketPath, ec);
        if (ec)
        {
            BMCWEB_LOG_WARNING(
                "unixSocketPath check failed sending internal server error",
                unixSocketPath.string());
            this->connection->sendStreamErrorStatus(
                boost::beast::http::status::internal_server_error);
            return;
        }
        if (fileExists)
        {
            if (std::remove(unixSocketPath.c_str()) != 0)
            {
                BMCWEB_LOG_WARNING("Failed to remove socket file: {}",
                                   unixSocketPath.string());
            }
        }
    }

    // NOLINTNEXTLINE
    void getDumpSize(const std::string& dumpEntryID,
                     const std::string& dumpEntryType)
    {
        std::string dumpEntryPath =
            std::format("/xyz/openbmc_project/dump/{}/entry/{}", dumpEntryType,
                        dumpEntryID);
        dbus::utility::getProperty<uint64_t>(
            "xyz.openbmc_project.Dump.Manager", dumpEntryPath,
            "xyz.openbmc_project.Dump.Entry", "Size",
            [this, self(weak_from_this())](const boost::system::error_code& ec,
                                           const uint64_t size) {
                auto sharedSelf = self.lock();
                if (!sharedSelf)
                {
                    return;
                }
                if (ec)
                {
                    if (ec.value() == EBADR)
                    {
                        this->connection->sendStreamErrorStatus(
                            boost::beast::http::status::not_found);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error: Unable to get the dump size {}",
                            ec.message());
                        this->connection->sendStreamErrorStatus(
                            boost::beast::http::status::internal_server_error);
                    }
                    this->connection->close();
                    return;
                }
                this->dumpSize = size;
                this->initiateOffload();
                this->doConnect();
            });
    }

    /**
     * @brief  Reads data from unix domain socket and writes on
     *         http stream connection socket.
     *
     * @return void
     */

    void doReadStream()
    {
        std::size_t bytes =
            this->outputBuffer.capacity() - this->outputBuffer.size();

        this->unixSocket.async_read_some(
            this->outputBuffer.prepare(bytes),
            [this, self(weak_from_this())](const boost::system::error_code& ec,
                                           std::size_t bytesRead) {
                auto sharedSelf = self.lock();
                if (!sharedSelf)
                {
                    return;
                }
                if (ec)
                {
                    if (ec != boost::asio::error::eof)
                    {
                        BMCWEB_LOG_ERROR("Couldn't read from local peer: {}",
                                         ec.message());
                        this->connection->sendStreamErrorStatus(
                            boost::beast::http::status::internal_server_error);
                        this->connection->close();
                        return;
                    }
                    BMCWEB_LOG_INFO("Hit Dump end of file calling close");
                    this->connection->completionStatus = true;
                    this->connection->close();
                    return;
                }

                this->outputBuffer.commit(bytesRead);
                auto streamHandler =
                    [this, bytesRead, self(weak_from_this())]() {
                        auto sharedSelfInner = self.lock();
                        if (!sharedSelfInner)
                        {
                            return;
                        }
                        this->outputBuffer.consume(bytesRead);
                        this->doReadStream();
                    };
                this->connection->sendMessage(this->outputBuffer.data(),
                                              streamHandler);
            });
    }

    std::string entryID;
    std::string dumpType;
    boost::beast::flat_static_buffer<socketBufferSize> outputBuffer;
    std::filesystem::path unixSocketPath;
    boost::asio::local::stream_protocol::socket unixSocket;
    uint64_t dumpSize{0};
    boost::asio::steady_timer waitTimer;
    std::shared_ptr<crow::streaming_response::Connection> connection;
    uint16_t connectRetryCount{0};
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static std::shared_ptr<Handler> systemHandlers = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

inline void resetHandlers()
{
    if (systemHandlers != nullptr)
    {
        if ((systemHandlers->dumpType == "system") ||
            (systemHandlers->dumpType == "resource"))
        {
            systemHandlers->connection->close();
            BMCWEB_LOG_INFO("Dump: {} dump resetHandlers cleanup",
                            systemHandlers->dumpType);
        }
    }
}

inline void requestRoutesDumpOffload(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/system/LogServices/Dump/Entries/<str>/attachment")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)
        .streamingResponse()
        .onopen([](crow::streaming_response::Connection& conn) {
            if (systemHandlers != nullptr)
            {
                BMCWEB_LOG_WARNING(
                    "Can't allow dump offload operation, one of the host dumps is already offloading");
                conn.sendStreamErrorStatus(
                    boost::beast::http::status::service_unavailable);
                conn.close();
                return;
            }

            std::string url(conn.req.target());
            std::string startDelimiter = "Entries/";
            std::size_t pos1 = url.rfind(startDelimiter);
            std::size_t pos2 = url.rfind("/attachment");
            if (pos1 == std::string::npos || pos2 == std::string::npos)
            {
                BMCWEB_LOG_WARNING("Unable to extract the dump id");
                conn.sendStreamErrorStatus(
                    boost::beast::http::status::not_found);
                conn.close();
                return;
            }
            std::string dumpId =
                url.substr(pos1 + startDelimiter.length(),
                           pos2 - pos1 - startDelimiter.length());
            std::string dumpType = "system";

            boost::asio::io_context* ioCon = &getIoContext();

            // Generating random id to create unique socket file
            // for each dump offload request
            std::random_device rd;
            std::default_random_engine gen(rd());
            std::uniform_int_distribution<> dist{0, 1024};
            std::string unixSocketPath = std::format(
                "{}{}_dump_{}", unixSocketPathDir, dumpType, dist(gen));
            if (!ioCon)
            {
                BMCWEB_LOG_ERROR("iocontext is null!");
                conn.sendStreamErrorStatus(
                    boost::beast::http::status::internal_server_error);
                conn.close();
                return;
            }

            try
            {
                systemHandlers = std::make_shared<Handler>(
                    *ioCon, dumpId, dumpType, unixSocketPath);
            }
            catch (const std::exception& e)
            {
                BMCWEB_LOG_ERROR("Exception while creating Handler: {}",
                                 e.what());
                systemHandlers = nullptr;
                conn.sendStreamErrorStatus(
                    boost::beast::http::status::internal_server_error);
                conn.close();
                return;
            }
            systemHandlers->connection = conn.shared_from_this();

            if (!crow::ibm_utils::createDirectory(unixSocketPathDir))
            {
                systemHandlers->connection->sendStreamErrorStatus(
                    boost::beast::http::status::not_found);
                systemHandlers->connection->close();
                return;
            }
            BMCWEB_LOG_INFO("Dump: {}  dump id {} offload initiated by: {}",
                            dumpType, dumpId, conn.req.session->clientIp);
            systemHandlers->getDumpSize(dumpId, dumpType);
        })
        .onclose([]([[maybe_unused]] crow::streaming_response::Connection& conn,
                    bool& status) {
            if (systemHandlers == nullptr)
            {
                BMCWEB_LOG_DEBUG("No handler to cleanup");
                return;
            }
            BMCWEB_LOG_DEBUG("cancelling AsyncOperations");
            // Cancel all async operations to release shared_ptr references
            systemHandlers->cancelAsyncOperations();
            systemHandlers->cleanupSocketFiles();
            if (!status)
            {
                systemHandlers->resetOffloadURI();
            }
            systemHandlers->outputBuffer.clear();
            systemHandlers.reset();
        });
}

} // namespace obmc_dump
} // namespace crow
