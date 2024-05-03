#pragma once
#include "app.hpp"
#include "async_resp.hpp"
#include "privileges.hpp"
#include "websocket.hpp"

#include <sys/socket.h>

#include <boost/asio/local/stream_protocol.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace crow
{
namespace obmc_console
{

// Update this value each time we add new console route.
static constexpr const uint maxSessions = 32;

class ConsoleHandler : public std::enable_shared_from_this<ConsoleHandler>
{
  public:
    ConsoleHandler(boost::asio::io_context& ioc,
                   crow::websocket::Connection& connIn) :
        hostSocket(ioc),
        conn(connIn)
    {}

    ~ConsoleHandler() = default;

    ConsoleHandler(const ConsoleHandler&) = delete;
    ConsoleHandler(ConsoleHandler&&) = delete;
    ConsoleHandler& operator=(const ConsoleHandler&) = delete;
    ConsoleHandler& operator=(ConsoleHandler&&) = delete;

    void doWrite()
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

        doingWrite = true;
        hostSocket.async_write_some(
            boost::asio::buffer(inputBuffer.data(), inputBuffer.size()),
            [weak(weak_from_this())](const boost::beast::error_code& ec,
                                     std::size_t bytesWritten) {
            std::shared_ptr<ConsoleHandler> self = weak.lock();
            if (self == nullptr)
            {
                return;
            }

            self->doingWrite = false;
            self->inputBuffer.erase(0, bytesWritten);

            if (ec == boost::asio::error::eof)
            {
                self->conn.close("Error in reading to host port");
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR << "Error in host serial write " << ec;
                return;
            }
            self->doWrite();
        });
    }

    static void afterSendEx(const std::weak_ptr<ConsoleHandler>& weak)
    {
        std::shared_ptr<ConsoleHandler> self = weak.lock();
        if (self == nullptr)
        {
            return;
        }
        self->doRead();
    }

    void doRead()
    {
        BMCWEB_LOG_DEBUG << "Reading from socket";
        hostSocket.async_read_some(
            boost::asio::buffer(outputBuffer),
            [this, weakSelf(weak_from_this())](
                const boost::system::error_code& ec, std::size_t bytesRead) {
            BMCWEB_LOG_DEBUG << "read done.  Read " << bytesRead << " bytes";
            std::shared_ptr<ConsoleHandler> self = weakSelf.lock();
            if (self == nullptr)
            {
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR << "Couldn't read from host serial port: "
                                 << ec.message();
                conn.close("Error connecting to host port");
                return;
            }
            std::string_view payload(outputBuffer.data(), bytesRead);
            self->conn.sendEx(crow::websocket::MessageType::Binary, payload,
                              std::bind_front(afterSendEx, weak_from_this()));
        });
    }

    void connect()
    {
        const std::string consoleName("\0obmc-console", 13);
        boost::asio::local::stream_protocol::endpoint ep(consoleName);

        hostSocket.async_connect(ep, [this, weakSelf(weak_from_this())](
                                         const boost::system::error_code& ec) {
            std::shared_ptr<ConsoleHandler> self = weakSelf.lock();
            if (self == nullptr)
            {
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR << "Failed to call console Connect() method"
                                 << " DBUS error: " << ec.message();

                conn.close("Internal Error");
                return;
            }

            doWrite();
            doRead();
        });
    }

    boost::asio::local::stream_protocol::socket hostSocket;

    std::array<char, 4096> outputBuffer{};

    std::string inputBuffer;
    bool doingWrite = false;
    crow::websocket::Connection& conn;
};

using ObmcConsoleMap = boost::container::flat_map<
    crow::websocket::Connection*, std::shared_ptr<ConsoleHandler>, std::less<>,
    std::vector<std::pair<crow::websocket::Connection*,
                          std::shared_ptr<ConsoleHandler>>>>;

inline ObmcConsoleMap& getConsoleHandlerMap()
{
    static ObmcConsoleMap map;
    return map;
}

// Remove connection from the connection map and if connection map is empty
// then remove the handler from handlers map.
inline void onClose(crow::websocket::Connection& conn, const std::string& err)
{
    BMCWEB_LOG_INFO << "Closing websocket. Reason: " << err;

    auto iter = getConsoleHandlerMap().find(&conn);
    if (iter == getConsoleHandlerMap().end())
    {
        BMCWEB_LOG_CRITICAL << "Unable to find connection " << &conn;
        return;
    }
    BMCWEB_LOG_DEBUG << "Remove connection " << &conn << " from obmc console";

    // Removed last connection so remove the path
    getConsoleHandlerMap().erase(iter);
}

inline void connectConsoleSocket(crow::websocket::Connection& conn)
{
    // Look up the handler
    auto iter = getConsoleHandlerMap().find(&conn);
    if (iter == getConsoleHandlerMap().end())
    {
        BMCWEB_LOG_ERROR << "Failed to find the handler";
        conn.close("Internal error");
        return;
    }

    iter->second->connect();
}

// Query consoles from DBUS and find the matching to the
// rules string.
inline void onOpen(crow::websocket::Connection& conn)
{
    BMCWEB_LOG_DEBUG << "Connection " << &conn << " opened";

    if (getConsoleHandlerMap().size() >= maxSessions)
    {
        conn.close("Max sessions are already connected");
        return;
    }

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
        auto userInfoIter = std::find_if(
            userInfo.begin(), userInfo.end(),
            [](const auto& p) { return p.first == "UserPrivilege"; });
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
            BMCWEB_LOG_DEBUG << "User " << conn.getUserName()
                             << " not authorized for host console connection";
            conn.close("Unauthorized access");
            return;
        }

        std::shared_ptr<ConsoleHandler> handler =
            std::make_shared<ConsoleHandler>(conn.getIoContext(), conn);
        getConsoleHandlerMap().emplace(&conn, handler);

        connectConsoleSocket(conn);
    };

    crow::connections::systemBus->async_method_call(
        std::move(getUserInfo), "xyz.openbmc_project.User.Manager",
        "/xyz/openbmc_project/user", "xyz.openbmc_project.User.Manager",
        "GetUserInfo", conn.getUserName());
}

inline void onMessage(crow::websocket::Connection& conn,
                      const std::string& data, bool /*isBinary*/)
{
    auto handler = getConsoleHandlerMap().find(&conn);
    if (handler == getConsoleHandlerMap().end())
    {
        BMCWEB_LOG_CRITICAL << "Unable to find connection " << &conn;
        return;
    }
    handler->second->inputBuffer += data;
    handler->second->doWrite();
}

inline void requestRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/console0")
        .privileges({{"ConfigureManager"}})
        .websocket()
        .onopen(onOpen)
        .onclose(onClose)
        .onmessage(onMessage);
}
} // namespace obmc_console
} // namespace crow
