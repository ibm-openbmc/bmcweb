#pragma once

#include "logging.hpp"
#include "mutual_tls.hpp"
#include "ssl_key_handler.hpp"

#include <openssl/ssl.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/system/error_code.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace bmcweb
{

inline int tlsVerifyCallback([[maybe_unused]] int preverified,
                             [[maybe_unused]] X509_STORE_CTX* ctx)
{
    BMCWEB_LOG_DEBUG("tlsVerifyCallback called with preverified {}",
                     preverified);

    if (!preverified)
    {
        BMCWEB_LOG_ERROR("mTLS: Client certificate verification failed");
    }

    return preverified;
}

// Returns true if the configured mTLS key location is a URI
inline bool isKeyUri(const std::string& keyLocation)
{
    return keyLocation.starts_with("handle:") ||
           keyLocation.find("://") != std::string::npos;
}

// Callable object for SNI context factory with state
struct SniContextFactoryState
{
    std::function<bool(const std::string&)> sniChecker;
    std::string mtlsCertFile;
    std::string mtlsKeyFile;
    std::string mtlsTrustStore;
    mutable std::optional<boost::asio::ssl::context> clientAuthContext;

    SniContextFactoryState(std::function<bool(const std::string&)> sniCheckerIn,
                           std::string mtlsCertFileIn,
                           std::string mtlsKeyFileIn,
                           std::string mtlsTrustStoreIn) :
        sniChecker(std::move(sniCheckerIn)),
        mtlsCertFile(std::move(mtlsCertFileIn)),
        mtlsKeyFile(std::move(mtlsKeyFileIn)),
        mtlsTrustStore(std::move(mtlsTrustStoreIn))
    {}

    // Copy constructor - context cannot be copied, so reset it
    SniContextFactoryState(const SniContextFactoryState& other) :
        sniChecker(other.sniChecker), mtlsCertFile(other.mtlsCertFile),
        mtlsKeyFile(other.mtlsKeyFile), mtlsTrustStore(other.mtlsTrustStore),
        clientAuthContext(std::nullopt)
    {}

    // Copy assignment operator
    SniContextFactoryState& operator=(const SniContextFactoryState& other)
    {
        if (this != &other)
        {
            sniChecker = other.sniChecker;
            mtlsCertFile = other.mtlsCertFile;
            mtlsKeyFile = other.mtlsKeyFile;
            mtlsTrustStore = other.mtlsTrustStore;
            clientAuthContext = std::nullopt;
        }
        return *this;
    }

    // Move constructor - move the context
    SniContextFactoryState(SniContextFactoryState&& other) noexcept :
        sniChecker(std::move(other.sniChecker)),
        mtlsCertFile(std::move(other.mtlsCertFile)),
        mtlsKeyFile(std::move(other.mtlsKeyFile)),
        mtlsTrustStore(std::move(other.mtlsTrustStore)),
        clientAuthContext(std::move(other.clientAuthContext))
    {}

    // Move assignment operator
    SniContextFactoryState& operator=(SniContextFactoryState&& other) noexcept
    {
        if (this != &other)
        {
            sniChecker = std::move(other.sniChecker);
            mtlsCertFile = std::move(other.mtlsCertFile);
            mtlsKeyFile = std::move(other.mtlsKeyFile);
            mtlsTrustStore = std::move(other.mtlsTrustStore);
            clientAuthContext = std::move(other.clientAuthContext);
        }
        return *this;
    }

    // Operator() to make this a callable object
    std::shared_ptr<boost::asio::ssl::context> operator()() const
    {
        clientAuthContext = std::nullopt;

        // Create primary SSL context (default/non-mTLS) using ensuressl
        auto primaryCtx = ensuressl::getSslServerContext();
        if (primaryCtx == nullptr)
        {
            BMCWEB_LOG_CRITICAL(
                "Failed to build the primary SSL context; TLS is unavailable");
            return nullptr;
        }

        // Set up SNI callback to switch contexts based on hostname
        SSL_CTX_set_tlsext_servername_callback(primaryCtx->native_handle(),
                                               sniCallbackStatic);
        SSL_CTX_set_tlsext_servername_arg(
            primaryCtx->native_handle(),
            const_cast<SniContextFactoryState*>(this));

        return primaryCtx;
    }

    // Create and configure client auth context with certificate verification
    bool createClientAuthContext() const
    {
        if (clientAuthContext)
        {
            return true;
        }
        try
        {
            clientAuthContext.emplace(boost::asio::ssl::context::tls_server);
            clientAuthContext->set_options(
                boost::asio::ssl::context::default_workarounds |
                boost::asio::ssl::context::no_sslv2 |
                boost::asio::ssl::context::no_sslv3 |
                boost::asio::ssl::context::no_tlsv1 |
                boost::asio::ssl::context::no_tlsv1_1 |
                boost::asio::ssl::context::single_dh_use);

            BMCWEB_LOG_INFO("Loading certificate from: {}", mtlsCertFile);
            if (ensuressl::isProviderCert(mtlsCertFile))
            {
                // Cert lives in a provider (e.g. a TPM NV index);
                // use_certificate_chain_file cannot read a provider, so load it
                // via OSSL_STORE as PEM and install the buffer.
                std::optional<std::string> pem =
                    ensuressl::loadCertPemFromUri(mtlsCertFile);
                if (!pem)
                {
                    BMCWEB_LOG_ERROR(
                        "Failed to load mTLS server cert from URI: {}",
                        mtlsCertFile);
                    clientAuthContext = std::nullopt;
                    return false;
                }
                boost::system::error_code ec;
                boost::asio::const_buffer certBuf(pem->data(), pem->size());
                clientAuthContext->use_certificate_chain(certBuf, ec);
                if (ec)
                {
                    BMCWEB_LOG_ERROR("Failed to install mTLS server cert: {}",
                                     ec.message());
                    clientAuthContext = std::nullopt;
                    return false;
                }
            }
            else
            {
                clientAuthContext->use_certificate_chain_file(mtlsCertFile);
            }

            BMCWEB_LOG_INFO("Loading private key from: {}", mtlsKeyFile);
            if (isKeyUri(mtlsKeyFile))
            {
                if (!ensuressl::loadPrivateKeyUriIntoContext(*clientAuthContext,
                                                             mtlsKeyFile))
                {
                    BMCWEB_LOG_ERROR(
                        "Failed to load mTLS server key from URI: {}",
                        mtlsKeyFile);
                    clientAuthContext = std::nullopt;
                    return false;
                }
            }
            else
            {
                clientAuthContext->use_private_key_file(
                    mtlsKeyFile, boost::asio::ssl::context::pem);
            }

            BMCWEB_LOG_INFO("Loading trust store from: {}", mtlsTrustStore);
            clientAuthContext->add_verify_path(mtlsTrustStore);

            clientAuthContext->set_verify_mode(
                boost::asio::ssl::verify_peer |
                boost::asio::ssl::verify_fail_if_no_peer_cert);

            BMCWEB_LOG_INFO("Client auth context created successfully");
            return true;
        }
        catch (const std::exception& e)
        {
            BMCWEB_LOG_ERROR("Exception creating client auth context: {}",
                             e.what());
            clientAuthContext = std::nullopt;
            return false;
        }
        catch (...)
        {
            BMCWEB_LOG_ERROR("Unknown exception creating client auth context");
            clientAuthContext = std::nullopt;
            return false;
        }
    }

    // Static SNI callback wrapper
    static int sniCallbackStatic(SSL* ssl, int* ad, void* arg)
    {
        auto* state = static_cast<SniContextFactoryState*>(arg);
        if (state != nullptr)
        {
            return state->sniCallback(ssl, ad);
        }
        return SSL_TLSEXT_ERR_OK;
    }

    // SNI callback function for switching between TLS and mTLS contexts
    int sniCallback(SSL* ssl, int* /*ad*/) const
    {
        const char* servername =
            SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

        if (servername == nullptr)
        {
            BMCWEB_LOG_DEBUG("SNI: No server name provided");
            return SSL_TLSEXT_ERR_OK;
        }

        std::string sni(servername);
        BMCWEB_LOG_DEBUG("SNI: Received server name: {}", sni);

        if (!sniChecker)
        {
            BMCWEB_LOG_DEBUG("SNI: No SNI checker function configured");
            return SSL_TLSEXT_ERR_OK;
        }

        // Use the sniChecker function to determine if we should switch to mTLS
        if (sniChecker(sni) && createClientAuthContext())
        {
            BMCWEB_LOG_INFO("SNI: Switching to mTLS context for {}", sni);

            SSL_set_SSL_CTX(ssl, clientAuthContext->native_handle());

            // After switching context, explicitly set verify mode and callback
            SSL_set_verify(ssl,
                           SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                           tlsVerifyCallback);

            BMCWEB_LOG_INFO(
                "SNI: Set verify mode and callback on SSL object for mTLS");
            BMCWEB_LOG_DEBUG(
                "SNI: Switched to mTLS context and configured callbacks");
            return SSL_TLSEXT_ERR_OK;
        }

        BMCWEB_LOG_DEBUG("SNI: Using standard TLS for {}", sni);
        return SSL_TLSEXT_ERR_OK;
    }
};

} // namespace bmcweb
