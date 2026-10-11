// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#pragma once

#include <openssl/crypto.h>

#include <boost/asio/ssl/context.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ensuressl
{

enum class VerifyCertificate
{
    Verify,
    NoVerify
};

constexpr const char* trustStorePath = "/etc/ssl/certs/authority";
constexpr const char* x509Comment = "Generated from OpenBMC service";

bool isTrustChainError(int errnum);

bool validateCertificate(X509* cert);

std::string verifyOpensslKeyCert(const std::string& filepath);

X509* loadCert(const std::string& filePath);

int addExt(X509* cert, int nid, const char* value);

std::string generateSslCertificate(const std::string& cn);

void writeCertificateToFile(const std::string& filepath,
                            const std::string& certificate);

std::string ensureOpensslKeyPresentAndValid(const std::string& filepath);

std::shared_ptr<boost::asio::ssl::context> getSslServerContext();

std::optional<boost::asio::ssl::context> getSSLClientContext(
    VerifyCertificate verifyCertificate);

// Loads a private key from a URI (file:// or a provider-backed scheme such as
// a TPM handle:) via the OpenSSL OSSL_STORE API and installs it into the SSL
// context. The certificate must already be set on the context. Returns false
// on failure.
bool loadPrivateKeyUriIntoContext(boost::asio::ssl::context& sslCtx,
                                  std::string_view uri);

// Resolves a certificate location (a file:// URI or a bare absolute filesystem
// path) to a filesystem path usable with use_certificate_chain_file. Provider
// schemes (e.g. a TPM handle:) are handled separately via loadCertPemFromUri;
// returns nullopt for those and other unsupported schemes.
std::optional<std::string> fileUriToPath(std::string_view uri);

// True if the certificate location is a provider object (e.g. a TPM NV index
// "handle:0x1500010") that must be read via OSSL_STORE rather than the
// filesystem. file:// URIs and bare paths are filesystem paths.
bool isProviderCert(std::string_view location);

// Loads a certificate from a provider URI (e.g. a TPM NV "handle:") via the
// OpenSSL OSSL_STORE API and returns it as a PEM string. This is the cert
// counterpart of loadPrivateKeyUriIntoContext; the returned PEM feeds the same
// use_certificate_chain path a filesystem cert does. Returns nullopt on
// failure.
std::optional<std::string> loadCertPemFromUri(const std::string& uri);

// Drains and logs the OpenSSL error queue with the given context prefix.
void logOpenSSLErrors(std::string_view context);

} // namespace ensuressl
