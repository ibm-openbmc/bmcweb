#pragma once

#include <pty.h>
#include <pwd.h>
#include <termios.h>

#include <app.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/process/io.hpp>
#include <websocket.hpp>

#include <map>

namespace crow
{

namespace obmc_shell
{

class Handler : public std::enable_shared_from_this<Handler>
{
  public:
    explicit Handler(crow::websocket::Connection* conn) :
        session(conn), streamFileDescriptor(conn->getIoContext())
    {
        outputBuffer.fill(0);
    }

    ~Handler() = default;

    Handler(const Handler&) = delete;
    Handler(Handler&&) = delete;
    Handler& operator=(const Handler&) = delete;
    Handler& operator=(Handler&&) = delete;

    void doClose()
    {
        streamFileDescriptor.close();

        // boost::process::child::terminate uses SIGKILL, need to send SIGTERM
        int rc = kill(pid, SIGKILL);
        session = nullptr;
        if (rc != 0)
        {
            return;
        }
        waitpid(pid, nullptr, 0);
    }

    void connect()
    {
        pid = forkpty(&ttyFileDescriptor, nullptr, nullptr, nullptr);

        if (pid == -1)
        {
            // ERROR
            if (session != nullptr)
            {
                session->close("Error creating child process for login shell.");
            }
            return;
        }
        if (pid == 0)
        {
            // CHILD

            if (auto userName = session->getUserName(); !userName.empty())
            {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
                execl("/bin/login", "/bin/login", "-f", userName.c_str(), NULL);
                // execl only returns on fail
                BMCWEB_LOG_ERROR << "execl() for /bin/login failed: " << errno;
                session->close("Internal Error Login failed");
            }
            else
            {
                session->close("Error session user name not found");
            }
            return;
        }
        if (pid > 0)
        {
            // PARENT

            // for io operation assing file discriptor
            // to boost stream_descriptor
            streamFileDescriptor.assign(ttyFileDescriptor);
            doWrite();
            doRead();
        }
    }

    void doWrite()
    {
        if (session == nullptr)
        {
            BMCWEB_LOG_DEBUG << "session is closed";
            return;
        }
        if (doingWrite)
        {
            BMCWEB_LOG_DEBUG << "Already writing.  Bailing out";
            return;
        }

        if (inputBuffer.empty())
        {
            BMCWEB_LOG_DEBUG << "inputBuffer empty.  Bailing out";
            return;
        }

        doingWrite = true;
        streamFileDescriptor.async_write_some(
            boost::asio::buffer(inputBuffer.data(), inputBuffer.size()),
            [this, self(shared_from_this())](boost::beast::error_code ec,
                                             std::size_t bytesWritten) {
            BMCWEB_LOG_DEBUG << "Wrote " << bytesWritten << "bytes";
            doingWrite = false;
            inputBuffer.erase(0, bytesWritten);
            if (ec == boost::asio::error::eof)
            {
                session->close("ssh socket port closed");
                return;
            }
            if (ec)
            {
                session->close("Error in writing to processSSH port");
                BMCWEB_LOG_ERROR << "Error in ssh socket write " << ec;
                return;
            }
            doWrite();
        });
    }

    void doRead()
    {
        if (session == nullptr)
        {
            BMCWEB_LOG_DEBUG << "session is closed";
            return;
        }
        streamFileDescriptor.async_read_some(
            boost::asio::buffer(outputBuffer.data(), outputBuffer.size()),
            [this, self(shared_from_this())](
                const boost::system::error_code& ec, std::size_t bytesRead) {
            BMCWEB_LOG_DEBUG << "Read done.  Read " << bytesRead << " bytes";
            if (session == nullptr)
            {
                BMCWEB_LOG_DEBUG << "session is closed";
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR << "Couldn't read from ssh port: " << ec;
                session->close("Error in connecting to ssh port");
                return;
            }
            std::string_view payload(outputBuffer.data(), bytesRead);
            session->sendBinary(payload);
            doRead();
        });
    }

    // this has to public
    std::string inputBuffer;

  private:
    crow::websocket::Connection* session;
    boost::asio::posix::stream_descriptor streamFileDescriptor;
    bool doingWrite{false};
    int ttyFileDescriptor{0};
    pid_t pid{0};

    std::array<char, 4096> outputBuffer{};
};

static std::map<crow::websocket::Connection*, std::shared_ptr<Handler>>
    mapHandler;

inline void requestRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/bmc-console")
        .privileges({{"OemIBMPerformService"}})
        .websocket()
        .onopen([](crow::websocket::Connection& conn) {
        BMCWEB_LOG_DEBUG << "Connection " << &conn << " opened";

        // TODO: this user check must be removed when WebSocket privileges
        // work.
        if (conn.getUserName() != "service")
        {
            BMCWEB_LOG_DEBUG << "only service user have access to obmc_shell";
            conn.close("only service user have access to bmc console");
        }
        if (auto it = mapHandler.find(&conn); it == mapHandler.end())
        {
            auto insertData =
                mapHandler.emplace(&conn, std::make_shared<Handler>(&conn));
            if (std::get<bool>(insertData))
            {
                std::get<0>(insertData)->second->connect();
            }
        }
    })
        .onclose(
            [](crow::websocket::Connection& conn, const std::string& reason) {
        BMCWEB_LOG_DEBUG << "bmc-shell console.onclose(reason = '" << reason
                         << "')";
        if (auto it = mapHandler.find(&conn); it != mapHandler.end())
        {
            it->second->doClose();
            mapHandler.erase(it);
        }
    })
        .onmessage([]([[maybe_unused]] crow::websocket::Connection& conn,
                      const std::string& data, [[maybe_unused]] bool isBinary) {
        if (auto it = mapHandler.find(&conn); it != mapHandler.end())
        {
            it->second->inputBuffer += data;
            it->second->doWrite();
        }
        else
        {
            BMCWEB_LOG_ERROR << "connection to webScoket not found";
        }
    });
}

} // namespace obmc_shell
} // namespace crow
