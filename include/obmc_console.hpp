#pragma once
#include <sys/socket.h>

#include <app.hpp>
#include <async_resp.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/container/flat_set.hpp>
#include <dbus_utility.hpp>
#include <privileges.hpp>
#include <websocket.hpp>

namespace crow
{
namespace obmc_console
{

static std::unique_ptr<boost::asio::local::stream_protocol::socket> hostSocket;

static std::array<char, 4096> outputBuffer;
static std::string inputBuffer;

static boost::container::flat_set<crow::websocket::Connection*> sessions;

static bool doingWrite = false;

inline void doWrite()
{
    if (doingWrite)
    {
        BMCWEB_LOG_DEBUG << "Already writing.  Bailing out";
        return;
    }

    if (inputBuffer.empty())
    {
        BMCWEB_LOG_DEBUG << "Outbuffer empty.  Bailing out";
        return;
    }

    if (!hostSocket)
    {
        BMCWEB_LOG_ERROR << "doWrite(): Socket closed.";
        return;
    }

    doingWrite = true;
    hostSocket->async_write_some(
        boost::asio::buffer(inputBuffer.data(), inputBuffer.size()),
        [](boost::beast::error_code ec, std::size_t bytesWritten) {
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
            BMCWEB_LOG_ERROR << "Error in host serial write " << ec;
            return;
        }
        doWrite();
        });
}

inline void doRead()
{
    if (!hostSocket)
    {
        BMCWEB_LOG_ERROR << "doRead(): Socket closed.";
        return;
    }

    BMCWEB_LOG_DEBUG << "Reading from socket";
    hostSocket->async_read_some(
        boost::asio::buffer(outputBuffer.data(), outputBuffer.size()),
        [](const boost::system::error_code& ec, std::size_t bytesRead) {
        BMCWEB_LOG_DEBUG << "read done.  Read " << bytesRead << " bytes";
        if (ec)
        {
            BMCWEB_LOG_ERROR << "Couldn't read from host serial port: " << ec;
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
        BMCWEB_LOG_ERROR << "Couldn't connect to host serial port: " << ec;
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
    BMCWEB_ROUTE(app, "/console0")
        .privileges({{"ConfigureManager"}})
        .websocket()
        .onopen([](crow::websocket::Connection& conn) {
            BMCWEB_LOG_DEBUG << "Connection " << &conn << " opened";
            // Ensure user has ConfigureManager, setting above does nothing
            auto getUserInfo =
                [&conn](const boost::system::error_code& ec,
                        const dbus::utility::DBusPropertiesMap& userInfo) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "GetUserInfo failed...";
                conn.close("Failed to get user information");
                return;
            }

            const std::string* userRolePtr = nullptr;
            auto userInfoIter = std::find_if(userInfo.begin(), userInfo.end(),
                                             [](const auto& p) {
                return p.first == "UserPrivilege";
            });
            if (userInfoIter != userInfo.end())
            {
                userRolePtr = std::get_if<std::string>(&userInfoIter->second);
            }

            std::string userRole{};
            if (userRolePtr != nullptr)
            {
                userRole = *userRolePtr;
                BMCWEB_LOG_DEBUG << "userName = " << conn.getUserName()
                                 << " userRole = " << *userRolePtr;
            }

            // Get the user privileges from the role
            ::redfish::Privileges userPrivileges =
                ::redfish::getUserPrivileges(userRole);

            const ::redfish::Privileges requiredPrivileges{"ConfigureManager"};

            if (!userPrivileges.isSupersetOf(requiredPrivileges))
            {
                BMCWEB_LOG_DEBUG
                    << "User " << conn.getUserName()
                    << " not authorized for host console connection";
                conn.close("Unathourized access");
                return;
            }

            sessions.insert(&conn);
            if (hostSocket == nullptr)
            {
                const std::string consoleName("\0obmc-console", 13);
                boost::asio::local::stream_protocol::endpoint ep(consoleName);

                hostSocket = std::make_unique<
                    boost::asio::local::stream_protocol::socket>(
                    conn.getIoContext());
                hostSocket->async_connect(ep, connectHandler);
            }
            };
            crow::connections::systemBus->async_method_call(
                std::move(getUserInfo), "xyz.openbmc_project.User.Manager",
                "/xyz/openbmc_project/user", "xyz.openbmc_project.User.Manager",
                "GetUserInfo", conn.getUserName());
        })
        .onclose([](crow::websocket::Connection& conn,
                    [[maybe_unused]] const std::string& reason) {
            BMCWEB_LOG_INFO << "Closing websocket. Reason: " << reason;

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
} // namespace obmc_console
} // namespace crow
