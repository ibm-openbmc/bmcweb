/*
// Copyright (c) 2020 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/basic_endpoint.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/version.hpp>
#include <boost/circular_buffer.hpp>
#include <include/async_resolve.hpp>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <string>

namespace crow
{

static constexpr uint8_t maxRequestQueueSize = 50;
static constexpr unsigned int httpReadBodyLimit = 8192;

enum class ConnState
{
    initialized,
    resolveInProgress,
    resolveFailed,
    connectInProgress,
    connectFailed,
    handshakeInProgress,
    handshakeFailed,
    connected,
    sendInProgress,
    sendFailed,
    recvInProgress,
    recvFailed,
    idle,
    closeInProgress,
    closed,
    suspended,
    terminated,
    abortConnection,
    retry
};

class HttpClient : public std::enable_shared_from_this<HttpClient>
{
  private:
    crow::async_resolve::Resolver resolver;
    boost::asio::ssl::context ctx{boost::asio::ssl::context::tlsv13_client};
    boost::beast::tcp_stream conn;
    std::optional<boost::beast::ssl_stream<boost::beast::tcp_stream&>> sslConn;
    boost::asio::steady_timer timer;
    boost::beast::flat_static_buffer<httpReadBodyLimit> buffer;
    boost::beast::http::request<boost::beast::http::string_body> req;
    std::optional<
        boost::beast::http::response_parser<boost::beast::http::string_body>>
        parser;
    boost::circular_buffer_space_optimized<std::string> requestDataQueue{
        maxRequestQueueSize};

    ConnState state = ConnState::initialized;

    std::string subId;
    std::string host;
    std::string port;
    uint32_t retryCount = 0;
    uint32_t maxRetryAttempts = 5;
    uint32_t retryIntervalSecs = 0;
    std::string retryPolicyAction = "TerminateAfterRetries";
    bool runningTimer = false;

    void doResolve()
    {
        state = ConnState::resolveInProgress;
        BMCWEB_LOG_DEBUG << "Trying to resolve: " << host << ":" << port;

        auto respHandler =
            [self(shared_from_this())](
                const boost::beast::error_code ec,
                const std::vector<boost::asio::ip::tcp::endpoint>&
                    endpointList) {
                if (ec || (endpointList.size() == 0))
                {
                    BMCWEB_LOG_ERROR << "Resolve failed: " << ec.message()
                                     << " Destination: " << self->host << ":"
                                     << self->port;
                    self->state = ConnState::resolveFailed;
                    self->handleConnState();
                    return;
                }
                BMCWEB_LOG_DEBUG << "Resolved";
                self->doConnect(endpointList);
            };
        resolver.asyncResolve(host, port, std::move(respHandler));
    }

    void doConnect(
        const std::vector<boost::asio::ip::tcp::endpoint>& endpointList)
    {
        state = ConnState::connectInProgress;
        sslConn.emplace(conn, ctx);

        BMCWEB_LOG_DEBUG << "Trying to connect to: " << host << ":" << port;
        auto respHandler = [self(shared_from_this())](
                               const boost::beast::error_code ec,
                               const boost::asio::ip::tcp::endpoint& endpoint) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG
                    << "Connect " << endpoint << " failed: " << ec.message()
                    << " Destination: " << self->host << ":" << self->port;
                self->state = ConnState::connectFailed;
                self->handleConnState();
                return;
            }

            BMCWEB_LOG_DEBUG << "Connected to: " << endpoint;
            if (self->sslConn)
            {
                self->performHandshake();
            }
            else
            {
                self->handleConnState();
            }
        };
        conn.expires_after(std::chrono::seconds(30));
        conn.async_connect(endpointList, std::move(respHandler));
    }

    void performHandshake()
    {
        state = ConnState::handshakeInProgress;

        sslConn->async_handshake(
            boost::asio::ssl::stream_base::client,
            [self(shared_from_this())](const boost::beast::error_code ec) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR << "SSL handshake failed: " << ec.message()
                                     << " Destination: " << self->host << ":"
                                     << self->port;
                    self->state = ConnState::handshakeFailed;
                    self->handleConnState();
                    return;
                }

                BMCWEB_LOG_DEBUG << "SSL Handshake successfull";
                self->state = ConnState::connected;
                self->handleConnState();
            });
    }

    void sendMessage(const std::string& data)
    {
        BMCWEB_LOG_DEBUG << __FUNCTION__ << "(): " << host << ":" << port;
        state = ConnState::sendInProgress;

        req.body() = data;
        req.prepare_payload();

        auto respHandler = [self(shared_from_this())](
                               const boost::beast::error_code ec,
                               const std::size_t& bytesTransferred) {
            if (ec)
            {
                auto eventData =
                    nlohmann::json::parse(self->req.body(), nullptr, false);
                uint64_t eventId;
                if (!eventData.is_discarded())
                {
                    for (const auto& item : eventData.items())
                    {
                        if (item.key() == "Id")
                        {
                            const uint64_t* id =
                                item.value().get_ptr<const uint64_t*>();
                            if (id != nullptr)
                            {
                                eventId = *id;
                            }
                        }
                    }
                }
                BMCWEB_LOG_ERROR << "sendMessage() failed: " << ec.message()
                                 << " Destination: " << self->host << ":"
                                 << self->port << " to subId: " << self->subId
                                 << " Event: " << eventId;
                self->state = ConnState::sendFailed;
                self->handleConnState();
                return;
            }

            BMCWEB_LOG_DEBUG << "sendMessage() bytes transferred: "
                             << bytesTransferred;
            boost::ignore_unused(bytesTransferred);
            self->recvMessage();
        };

        if (sslConn)
        {
            boost::beast::http::async_write(*sslConn, req,
                                            std::move(respHandler));
        }
        else
        {
            boost::beast::http::async_write(conn, req, std::move(respHandler));
        }
    }
    void recvMessage()
    {
        state = ConnState::recvInProgress;

        auto respHandler = [self(shared_from_this())](
                               const boost::beast::error_code ec,
                               const std::size_t& bytesTransferred) {
            auto eventData =
                nlohmann::json::parse(self->req.body(), nullptr, false);
            uint64_t eventId;
            if (!eventData.is_discarded())
            {
                for (const auto& item : eventData.items())
                {
                    if (item.key() == "Id")
                    {
                        const uint64_t* id =
                            item.value().get_ptr<const uint64_t*>();
                        if (id != nullptr)
                        {
                            eventId = *id;
                        }
                    }
                }
            }
            if (ec && ec != boost::asio::ssl::error::stream_truncated)
            {
                BMCWEB_LOG_ERROR << "recvMessage() failed: " << ec.message()
                                 << " from subId: " << self->subId
                                 << " Destination: " << self->host << ":"
                                 << self->port << " for Event: " << eventId;
                self->state = ConnState::recvFailed;
                self->handleConnState();
                return;
            }

            BMCWEB_LOG_DEBUG << "recvMessage() bytes transferred: "
                             << bytesTransferred;
            boost::ignore_unused(bytesTransferred);

            // Check if the response and header are received
            if (!self->parser->is_done())
            {
                // The parser failed to receive the response
                BMCWEB_LOG_ERROR
                    << "recvMessage() parser failed to receive response"
                    << " from subId: " << self->subId
                    << " Destination: " << self->host << ":" << self->port
                    << " for Event: " << eventId;
                self->state = ConnState::recvFailed;
                self->handleConnState();
                return;
            }

            unsigned int respCode = self->parser->get().result_int();
            BMCWEB_LOG_DEBUG << "recvMessage() Header Response Code: "
                             << respCode;

            // 2XX response is considered to be successful
            if ((respCode < 200) || (respCode >= 300))
            {
                // The listener failed to receive the Sent-Event
                BMCWEB_LOG_ERROR << "recvMessage() Listener Failed to "
                                    "receive Sent-Event. Header Response Code: "
                                 << respCode << " from subId: " << self->subId
                                 << " Destination: " << self->host << ":"
                                 << self->port << " for Event: " << eventId;
                self->state = ConnState::recvFailed;
                self->handleConnState();
                return;
            }

            // Send is successful, Lets remove data from queue
            // check for next request data in queue.
            if (!self->requestDataQueue.empty())
            {
                self->requestDataQueue.pop_front();
            }
            self->state = ConnState::idle;
            // Keep the connection alive if server supports it
            // Else close the connection
            BMCWEB_LOG_DEBUG << "recvMessage() keepalive : "
                             << self->parser->keep_alive();
            if (!self->parser->keep_alive())
            {
                // Abort the connection since server is not keep-alive
                // enabled
                self->state = ConnState::abortConnection;
            }

            // Returns ownership of the parsed message
            self->parser->release();

            // Set the retry count to 0 as the send-recv was successful
            self->retryCount = 0;

            self->handleConnState();
        };
        parser.emplace(std::piecewise_construct, std::make_tuple());
        parser->body_limit(httpReadBodyLimit);

        // Check only for the response header
        parser->skip(true);
        conn.expires_after(std::chrono::seconds(30));
        if (sslConn)
        {
            boost::beast::http::async_read(*sslConn, buffer, *parser,
                                           std::move(respHandler));
        }
        else
        {
            boost::beast::http::async_read(conn, buffer, *parser,
                                           std::move(respHandler));
        }
    }
    void doClose()
    {
        state = ConnState::closeInProgress;

        if (sslConn)
        {
            sslConn->async_shutdown([self = shared_from_this()](
                                        const boost::system::error_code ec) {
                if (ec)
                {
                    // Many https server closes connection abruptly
                    // i.e witnout close_notify. More details are at
                    // https://github.com/boostorg/beast/issues/824
                    if (ec == boost::asio::ssl::error::stream_truncated)
                    {
                        BMCWEB_LOG_INFO << "doClose(): Connection "
                                           "closed by server. ";
                    }
                    else
                    {
                        BMCWEB_LOG_DEBUG << "doClose() failed: " << ec.message()
                                         << " Destination: " << self->host
                                         << ":" << self->port;
                    }
                }
                else
                {
                    BMCWEB_LOG_DEBUG << "INFO: Connection closed gracefully..."
                                     << " Destination: " << self->host << ":"
                                     << self->port;
                }
                self->conn.close();

                if ((self->state != ConnState::suspended) &&
                    (self->state != ConnState::terminated))
                {
                    self->state = ConnState::closed;
                    self->handleConnState();
                }
            });
        }
        else
        {
            boost::beast::error_code ec;
            conn.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both,
                                   ec);
            if (ec)
            {
                BMCWEB_LOG_ERROR << "doClose() failed: " << ec.message();
            }
            else
            {
                BMCWEB_LOG_DEBUG << "Connection closed gracefully...";
            }
            conn.close();

            if ((state != ConnState::suspended) &&
                (state != ConnState::terminated))
            {
                state = ConnState::closed;
                handleConnState();
            }
        }
    }

    void waitAndRetry()
    {
        if (retryCount >= maxRetryAttempts)
        {
            BMCWEB_LOG_ERROR
                << "Maximum number of retries reached for Subscriber:" << subId;

            if (retryPolicyAction == "TerminateAfterRetries")
            {
                // Clear queue.
                while (!requestDataQueue.empty())
                {
                    requestDataQueue.pop_front();
                }

                state = ConnState::terminated;
                // TODO :Remove the subscription
                BMCWEB_LOG_ERROR << "TerminateAfterRetries is set. retryCount: "
                                 << retryCount << " .Subscriber: " << subId;
                return;
            }
            if (retryPolicyAction == "SuspendRetries")
            {
                // Clear queue.
                while (!requestDataQueue.empty())
                {
                    requestDataQueue.pop_front();
                }

                state = ConnState::suspended;
                BMCWEB_LOG_ERROR
                    << "SuspendRetries is set. retryCount: " << retryCount
                    << " .Subscriber: " << subId << "suspended";
                return;
            }

            BMCWEB_LOG_DEBUG << retryPolicyAction
                             << " is set. Cleanup the current event and "
                                "reset retrycount for subId: "
                             << subId;
            // Reset the retrycount to zero so that client can try connecting
            // again if needed
            retryCount = 0;

            // All retries failed, Lets remove data from queue
            if (!requestDataQueue.empty())
            {
                requestDataQueue.pop_front();
            }

            // Reset the connection state to Idle, and try sending the next
            // event in the queue
            state = ConnState::idle;

            handleConnState();
            return;
        }

        if (runningTimer)
        {
            BMCWEB_LOG_DEBUG << "Retry timer is already running.";
            return;
        }
        runningTimer = true;

        retryCount++;

        BMCWEB_LOG_DEBUG << "Attempt retry after " << retryIntervalSecs
                         << " seconds. RetryCount = " << retryCount;
        timer.expires_after(std::chrono::seconds(retryIntervalSecs));
        timer.async_wait(
            [self = shared_from_this()](const boost::system::error_code ec) {
                if (ec == boost::asio::error::operation_aborted)
                {
                    BMCWEB_LOG_DEBUG
                        << "async_wait failed since the operation is aborted"
                        << ec.message();
                }
                else if (ec)
                {
                    BMCWEB_LOG_ERROR << "async_wait failed: " << ec.message();
                    // Ignore the error and continue the retry loop to attempt
                    // sending the event as per the retry policy
                }
                self->runningTimer = false;

                // Lets close connection and start from resolve.
                self->doClose();
            });
        return;
    }

    void handleConnState()
    {
        switch (state)
        {
            case ConnState::resolveInProgress:
            case ConnState::connectInProgress:
            case ConnState::handshakeInProgress:
            case ConnState::sendInProgress:
            case ConnState::recvInProgress:
            case ConnState::closeInProgress:
            {
                BMCWEB_LOG_DEBUG << "Async operation is already in progress";
                break;
            }
            case ConnState::initialized:
            case ConnState::closed:
            {
                if (requestDataQueue.empty())
                {
                    BMCWEB_LOG_DEBUG << "requestDataQueue is empty";
                    return;
                }
                doResolve();
                break;
            }
            case ConnState::suspended:
            {
                doClose();
                break;
            }
            case ConnState::terminated:
            {
                BMCWEB_LOG_ERROR << "Subscriber " << subId
                                 << " connection terminated. Stop";
                break;
            }
            case ConnState::resolveFailed:
            case ConnState::connectFailed:
            case ConnState::handshakeFailed:
            case ConnState::sendFailed:
            case ConnState::recvFailed:
            case ConnState::retry:
            {
                // In case of failures during connect and handshake
                // the retry policy will be applied
                waitAndRetry();
                break;
            }
            case ConnState::connected:
            case ConnState::idle:
            {
                // State idle means, previous attempt is successful
                // State connected means, client connection is established
                // successfully
                if (requestDataQueue.empty())
                {
                    BMCWEB_LOG_DEBUG << "requestDataQueue is empty";
                    return;
                }
                std::string data = requestDataQueue.front();
                sendMessage(data);
                break;
            }
            case ConnState::abortConnection:
            {
                // Server did not want to keep alive the session
                doClose();
                break;
            }
            default:
                break;
        }
    }

  public:
    explicit HttpClient(boost::asio::io_context& ioc, const std::string& id,
                        const std::string& destIP, const std::string& destPort,
                        const std::string& destUri, const std::string& uriProto,
                        const boost::beast::http::fields& httpHeader) :
        conn(ioc),
        timer(ioc),
        req(boost::beast::http::verb::post, destUri, 11, "", httpHeader),
        subId(id), host(destIP), port(destPort)
    {
        // Set the request header
        req.set(boost::beast::http::field::host, host);
        req.set(boost::beast::http::field::content_type, "application/json");
        req.keep_alive(true);

        requestDataQueue.set_capacity(maxRequestQueueSize);
        if (uriProto == "https")
        {
            ctx.set_options(boost::asio::ssl::context::no_tlsv1_1);
            sslConn.emplace(conn, ctx);
        }
    }
    void sendData(const std::string& data)
    {
        auto eventData = nlohmann::json::parse(data, nullptr, false);
        uint64_t eventId;
        if (!eventData.is_discarded())
        {
            for (const auto& item : eventData.items())
            {
                if (item.key() == "Id")
                {
                    const uint64_t* id =
                        item.value().get_ptr<const uint64_t*>();
                    if (id != nullptr)
                    {
                        eventId = *id;
                    }
                }
            }
        }

        if ((state == ConnState::suspended) || (state == ConnState::terminated))
        {
            BMCWEB_LOG_ERROR << "sendData: " << subId
                             << " ConnState is suspended or terminated."
                             << " Destination: " << host << ":" << port
                             << " Event: " << eventId;
            return;
        }

        if (requestDataQueue.size() <= maxRequestQueueSize)
        {
            requestDataQueue.push_back(data);
            handleConnState();
        }
        else
        {
            BMCWEB_LOG_ERROR << "Request queue is full. So ignoring data."
                             << " Event: " << eventId;
        }

        return;
    }

    void setRetryConfig(const uint32_t retryAttempts,
                        const uint32_t retryTimeoutInterval)
    {
        maxRetryAttempts = retryAttempts;
        retryIntervalSecs = retryTimeoutInterval;
    }

    void setRetryPolicy(const std::string& retryPolicy)
    {
        retryPolicyAction = retryPolicy;
    }

    ConnState getConnState()
    {
        return state;
    }
};

} // namespace crow
