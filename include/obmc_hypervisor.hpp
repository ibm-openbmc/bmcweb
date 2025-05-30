#pragma once

#include "app.hpp"
#include "io_context_singleton.hpp"
#include "logging.hpp"
#include "websocket.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace crow
{
namespace obmc_hypervisor
{

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<boost::asio::local::stream_protocol::socket> hostSocket;

static std::array<char, 4096> outputBuffer;
static std::string inputBuffer;

static boost::container::flat_set<crow::websocket::Connection*> sessions;

static bool doingWrite = false;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

inline void doWrite()
{
    if (doingWrite)
    {
        BMCWEB_LOG_DEBUG("Already writing.  Bailing out");
        return;
    }

    if (inputBuffer.empty())
    {
        BMCWEB_LOG_DEBUG("Outbuffer empty.  Bailing out");
        return;
    }

    if (!hostSocket)
    {
        BMCWEB_LOG_ERROR("doWrite(): Socket closed.");
        return;
    }

    doingWrite = true;
    hostSocket->async_write_some(
        boost::asio::buffer(inputBuffer.data(), inputBuffer.size()),
        [](const boost::beast::error_code& ec, std::size_t bytesWritten) {
            doingWrite = false;
            inputBuffer.erase(0, bytesWritten);

            if (ec == boost::asio::error::eof)
            {
                for (crow::websocket::Connection* session : sessions)
                {
                    session->close("Error in reading to host port");
                }
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error in host serial write {}", ec);
                return;
            }
            doWrite();
        });
}

inline void doRead()
{
    if (!hostSocket)
    {
        BMCWEB_LOG_ERROR("doRead(): Socket closed.");
        return;
    }

    BMCWEB_LOG_DEBUG("Reading from socket");
    hostSocket->async_read_some(
        boost::asio::buffer(outputBuffer.data(), outputBuffer.size()),
        [](const boost::system::error_code& ec, std::size_t bytesRead) {
            BMCWEB_LOG_DEBUG("read done.  Read {} bytes", bytesRead);
            if (ec)
            {
                BMCWEB_LOG_ERROR("Couldn't read from host serial port: {}", ec);
                for (crow::websocket::Connection* session : sessions)
                {
                    session->close("Error in connecting to host port");
                }
                return;
            }
            std::string_view payload(outputBuffer.data(), bytesRead);
            for (crow::websocket::Connection* session : sessions)
            {
                session->sendBinary(payload);
            }
            doRead();
        });
}

inline void connectHandler(const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Couldn't connect to host serial port: {}", ec);
        for (crow::websocket::Connection* session : sessions)
        {
            session->close("Error in connecting to host port");
        }
        return;
    }

    doWrite();
    doRead();
}

inline void requestRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/console1")
        .privileges({{"OemIBMPerformService"}})
        .websocket()
        .onopen([](crow::websocket::Connection& conn) {
            BMCWEB_LOG_DEBUG("Connection {} opened", logPtr(&conn));

            sessions.insert(&conn);
            if (hostSocket == nullptr)
            {
                const std::string consoleName("\0obmc-console.hypervisor", 24);
                boost::asio::local::stream_protocol::endpoint ep(consoleName);

                hostSocket = std::make_unique<
                    boost::asio::local::stream_protocol::socket>(
                    getIoContext());
                hostSocket->async_connect(ep, connectHandler);
            }
        })
        .onclose([](crow::websocket::Connection& conn,
                    [[maybe_unused]] const std::string& reason) {
            BMCWEB_LOG_INFO("Closing websocket. Reason: {}", reason);

            sessions.erase(&conn);
            if (sessions.empty())
            {
                hostSocket = nullptr;
                inputBuffer.clear();
                inputBuffer.shrink_to_fit();
            }
        })
        .onmessage([]([[maybe_unused]] crow::websocket::Connection& conn,
                      const std::string& data, [[maybe_unused]] bool isBinary) {
            inputBuffer += data;
            doWrite();
        });
}
} // namespace obmc_hypervisor
} // namespace crow
