// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "ssl_key_handler.hpp"

#include "bmcweb_config.h"

#include "forward_unauthorized.hpp"
#include "logging.hpp"
#include "ossl_random.hpp"
#include "sessions.hpp"
#include <boost/asio/buffer.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/beast/core/file_base.hpp>
#include <boost/beast/core/file_posix.hpp>
#include <boost/system/error_code.hpp>
#include <fstream>
extern "C"
{
#include <nghttp2/nghttp2.h>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>
#include <openssl/ssl.h>
}

#include <bit>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <utility>

extern "C" int sniDummyCallback(SSL* ssl, int* al, void* arg)
{
    (void)ssl;
    (void)al;
    (void)arg;
        return SSL_CLIENT_HELLO_SUCCESS;
}


namespace ensuressl
{

namespace crow
{

std::shared_ptr<boost::asio::ssl::context> g_httpsCtx = nullptr;
std::shared_ptr<boost::asio::ssl::context> g_mtlsCtx = nullptr;
}
bool isSslCtxMtlsForSsl(SSL* ssl);
static EVP_PKEY* createEcKey();

// Mozilla intermediate cipher suites v5.7
// Sourced from: https://ssl-config.mozilla.org/guidelines/5.7.json
constexpr const char* mozillaIntermediate =
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-CHACHA20-POLY1305:"
    "ECDHE-RSA-CHACHA20-POLY1305:"
    "DHE-RSA-AES128-GCM-SHA256:"
    "DHE-RSA-AES256-GCM-SHA384:"
    "DHE-RSA-CHACHA20-POLY1305";

// Trust chain related errors.`
bool isTrustChainError(int errnum)
{
    return (errnum == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) ||
           (errnum == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN) ||
           (errnum == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY) ||
           (errnum == X509_V_ERR_CERT_UNTRUSTED) ||
           (errnum == X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE);
}

bool validateCertificate(X509* const cert)
{
    // Create an empty X509_STORE structure for certificate validation.
    X509_STORE* x509Store = X509_STORE_new();
    if (x509Store == nullptr)
    {
        BMCWEB_LOG_ERROR("Error occurred during X509_STORE_new call");
        return false;
    }

    // Load Certificate file into the X509 structure.
    X509_STORE_CTX* storeCtx = X509_STORE_CTX_new();
    if (storeCtx == nullptr)
    {
        BMCWEB_LOG_ERROR("Error occurred during X509_STORE_CTX_new call");
        X509_STORE_free(x509Store);
        return false;
    }

    int errCode = X509_STORE_CTX_init(storeCtx, x509Store, cert, nullptr);
    if (errCode != 1)
    {
        BMCWEB_LOG_ERROR("Error occurred during X509_STORE_CTX_init call");
        X509_STORE_CTX_free(storeCtx);
        X509_STORE_free(x509Store);
        return false;
    }

    errCode = X509_verify_cert(storeCtx);
    if (errCode == 1)
    {
        BMCWEB_LOG_INFO("Certificate verification is success");
        X509_STORE_CTX_free(storeCtx);
        X509_STORE_free(x509Store);
        return true;
    }
    if (errCode == 0)
    {
        errCode = X509_STORE_CTX_get_error(storeCtx);
        X509_STORE_CTX_free(storeCtx);
        X509_STORE_free(x509Store);
        if (isTrustChainError(errCode))
        {
            BMCWEB_LOG_DEBUG("Ignoring Trust Chain error. Reason: {}",
                             X509_verify_cert_error_string(errCode));
            return true;
        }
        BMCWEB_LOG_ERROR("Certificate verification failed. Reason: {}",
                         X509_verify_cert_error_string(errCode));
        return false;
    }

    BMCWEB_LOG_ERROR(
        "Error occurred during X509_verify_cert call. ErrorCode: {}", errCode);
    X509_STORE_CTX_free(storeCtx);
    X509_STORE_free(x509Store);
    return false;
}

std::string verifyOpensslKeyCert(const std::string& filepath)
{

    bool privateKeyValid = false;
    BMCWEB_LOG_INFO("Checking certs in file {}", filepath);
    boost::beast::file_posix file;
    boost::system::error_code ec;
    file.open(filepath.c_str(), boost::beast::file_mode::read, ec);
    if (ec)
    {
        return "";
    }
    bool certValid = false;
    std::string fileContents;
    fileContents.resize(static_cast<size_t>(file.size(ec)), '\0');
    file.read(fileContents.data(), fileContents.size(), ec);
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to read file");
        return "";
    }

    BIO* bufio = BIO_new_mem_buf(static_cast<void*>(fileContents.data()),
                                 static_cast<int>(fileContents.size()));
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bufio, nullptr, nullptr, nullptr);
    BIO_free(bufio);
    if (pkey != nullptr)
    {
        EVP_PKEY_CTX* pkeyCtx =
            EVP_PKEY_CTX_new_from_pkey(nullptr, pkey, nullptr);

        if (pkeyCtx == nullptr)
        {
            BMCWEB_LOG_ERROR("Unable to allocate pkeyCtx {}", ERR_get_error());
        }
        else if (EVP_PKEY_check(pkeyCtx) == 1)
        {
            privateKeyValid = true;
        }
        else
        {
            BMCWEB_LOG_ERROR("Key not valid error number {}", ERR_get_error());
        }

        if (privateKeyValid)
        {
            BIO* bufio2 =
                BIO_new_mem_buf(static_cast<void*>(fileContents.data()),
                                static_cast<int>(fileContents.size()));
            X509* x509 = PEM_read_bio_X509(bufio2, nullptr, nullptr, nullptr);
            BIO_free(bufio2);
            if (x509 == nullptr)
            {
                BMCWEB_LOG_ERROR("error getting x509 cert {}", ERR_get_error());
            }
            else
            {
                certValid = validateCertificate(x509);
                X509_free(x509);
            }
        }

        EVP_PKEY_CTX_free(pkeyCtx);
        EVP_PKEY_free(pkey);
    }
    if (!certValid)
    {
        return "";
    }
    return fileContents;
}

X509* loadCert(const std::string& filePath)
{
    BIO* certFileBio = BIO_new_file(filePath.c_str(), "rb");
    if (certFileBio == nullptr)
    {
        BMCWEB_LOG_ERROR("Error occurred during BIO_new_file call, FILE= {}",
                         filePath);
        return nullptr;
    }

    X509* cert = X509_new();
    if (cert == nullptr)
    {
        BMCWEB_LOG_ERROR("Error occurred during X509_new call, {}",
                         ERR_get_error());
        BIO_free(certFileBio);
        return nullptr;
    }

    if (PEM_read_bio_X509(certFileBio, &cert, nullptr, nullptr) == nullptr)
    {
        BMCWEB_LOG_ERROR(
            "Error occurred during PEM_read_bio_X509 call, FILE= {}", filePath);

        BIO_free(certFileBio);
        X509_free(cert);
        return nullptr;
    }
    BIO_free(certFileBio);
    return cert;
}

int addExt(X509* cert, int nid, const char* value)
{
    X509_EXTENSION* ex = nullptr;
    X509V3_CTX ctx{};
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    ex = X509V3_EXT_conf_nid(nullptr, &ctx, nid, const_cast<char*>(value));
    if (ex == nullptr)
    {
        BMCWEB_LOG_ERROR("Error: In X509V3_EXT_conf_nidn: {}", value);
        return -1;
    }
    X509_add_ext(cert, ex, -1);
    X509_EXTENSION_free(ex);
    return 0;
}

// Writes a certificate to a path, ignoring errors
void writeCertificateToFile(const std::string& filepath,
                            const std::string& certificate)
{
    boost::system::error_code ec;
    boost::beast::file_posix file;
    file.open(filepath.c_str(), boost::beast::file_mode::write, ec);
    if (!ec)
    {
        file.write(certificate.data(), certificate.size(), ec);
        // ignore result
    }
}

std::string generateSslCertificate(const std::string& cn)
{
    BMCWEB_LOG_INFO("Generating new keys");

    std::string buffer;
    BMCWEB_LOG_INFO("Generating EC key");
    EVP_PKEY* pPrivKey = createEcKey();
    if (pPrivKey != nullptr)
    {
        BMCWEB_LOG_INFO("Generating x509 Certificates");
        // Use this code to directly generate a certificate
        X509* x509 = X509_new();
        if (x509 != nullptr)
        {
            // get a random number from the RNG for the certificate serial
            // number If this is not random, regenerating certs throws browser
            // errors
            bmcweb::OpenSSLGenerator gen;
            std::uniform_int_distribution<int> dis(
                1, std::numeric_limits<int>::max());
            int serial = dis(gen);

            ASN1_INTEGER_set(X509_get_serialNumber(x509), serial);

            // not before this moment
            X509_gmtime_adj(X509_get_notBefore(x509), 0);
            // Cert is valid for 10 years
            X509_gmtime_adj(X509_get_notAfter(x509),
                            60L * 60L * 24L * 365L * 10L);

            // set the public key to the key we just generated
            X509_set_pubkey(x509, pPrivKey);

            // get the subject name
            X509_NAME* name = X509_get_subject_name(x509);

            using x509String = const unsigned char;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            x509String* country = reinterpret_cast<x509String*>("US");
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            x509String* company = reinterpret_cast<x509String*>("OpenBMC");
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            x509String* cnStr = reinterpret_cast<x509String*>(cn.c_str());

            X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, country, -1, -1,
                                       0);
            X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, company, -1, -1,
                                       0);
            X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, cnStr, -1, -1,
                                       0);
            // set the CSR options
            X509_set_issuer_name(x509, name);

            X509_set_version(x509, 2);
            addExt(x509, NID_basic_constraints, ("critical,CA:TRUE"));
            addExt(x509, NID_subject_alt_name, ("DNS:" + cn).c_str());
            addExt(x509, NID_subject_key_identifier, ("hash"));
            addExt(x509, NID_authority_key_identifier, ("keyid"));
            addExt(x509, NID_key_usage, ("digitalSignature, keyEncipherment"));
            addExt(x509, NID_ext_key_usage, ("serverAuth"));
            addExt(x509, NID_netscape_comment, (x509Comment));

            // Sign the certificate with our private key
            X509_sign(x509, pPrivKey, EVP_sha256());

            BIO* bufio = BIO_new(BIO_s_mem());

            int pkeyRet = PEM_write_bio_PrivateKey(
                bufio, pPrivKey, nullptr, nullptr, 0, nullptr, nullptr);
            if (pkeyRet <= 0)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to write pkey with code {}.  Ignoring.", pkeyRet);
            }

            char* data = nullptr;
            long int dataLen = BIO_get_mem_data(bufio, &data);
            buffer += std::string_view(data, static_cast<size_t>(dataLen));
            BIO_free(bufio);

            bufio = BIO_new(BIO_s_mem());
            pkeyRet = PEM_write_bio_X509(bufio, x509);
            if (pkeyRet <= 0)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to write X509 with code {}.  Ignoring.", pkeyRet);
            }
            dataLen = BIO_get_mem_data(bufio, &data);
            buffer += std::string_view(data, static_cast<size_t>(dataLen));

            BIO_free(bufio);
            BMCWEB_LOG_INFO("Cert size is {}", buffer.size());
            X509_free(x509);
        }

        EVP_PKEY_free(pPrivKey);
        pPrivKey = nullptr;
    }

    // cleanup_openssl();
    return buffer;
}

EVP_PKEY* createEcKey()
{
    EVP_PKEY* pKey = nullptr;

    // Create context for curve parameter generation.
    std::unique_ptr<EVP_PKEY_CTX, decltype(&::EVP_PKEY_CTX_free)> ctx{
        EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), &::EVP_PKEY_CTX_free};
    if (!ctx)
    {
        return nullptr;
    }

    // Set up curve parameters.
    EVP_PKEY* params = nullptr;
    if ((EVP_PKEY_paramgen_init(ctx.get()) <= 0) ||
        (EVP_PKEY_CTX_set_ec_param_enc(ctx.get(), OPENSSL_EC_NAMED_CURVE) <=
         0) ||
        (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx.get(), NID_secp384r1) <=
         0) ||
        (EVP_PKEY_paramgen(ctx.get(), &params) <= 0))
    {
        return nullptr;
    }

    // Set up RAII holder for params.
    std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> pparams{
        params, &::EVP_PKEY_free};

    // Set new context for key generation, using curve parameters.
    ctx.reset(EVP_PKEY_CTX_new_from_pkey(nullptr, params, nullptr));
    if (!ctx || (EVP_PKEY_keygen_init(ctx.get()) <= 0))
    {
        return nullptr;
    }

    // Generate key.
    if (EVP_PKEY_keygen(ctx.get(), &pKey) <= 0)
    {
        return nullptr;
    }

    return pKey;
}

std::string ensureOpensslKeyPresentAndValid(const std::string& filepath)
{
    std::string cert = verifyOpensslKeyCert(filepath);

    if (cert.empty())
    {
        BMCWEB_LOG_WARNING("Error in verifying signature, regenerating");
        cert = generateSslCertificate("testhost");
        if (cert.empty())
        {
            BMCWEB_LOG_ERROR("Failed to generate cert");
        }
        else
        {
            writeCertificateToFile(filepath, cert);
        }
    }
    return cert;
}

static std::string ensureCertificate()
{
    namespace fs = std::filesystem;
    // Cleanup older certificate file existing in the system
    fs::path oldcertPath = fs::path("/home/root/server.pem");
    std::error_code ec;
    fs::remove(oldcertPath, ec);
    // Ignore failure to remove;  File might not exist.

    fs::path certPath = "/etc/ssl/certs/https/";
    // if path does not exist create the path so that
    // self signed certificate can be created in the
    // path
    fs::path certFile = certPath / "server.pem";

    if (!fs::exists(certPath, ec))
    {
        fs::create_directories(certPath, ec);
    }
    BMCWEB_LOG_INFO("Building SSL Context file= {}", certFile.string());
    std::string sslPemFile(certFile);
    return ensuressl::ensureOpensslKeyPresentAndValid(sslPemFile);
}

static int nextProtoCallback(SSL* /*unused*/, const unsigned char** data,
                             unsigned int* len, void* /*unused*/)
{
    // First byte is the length.
    constexpr std::string_view h2 = "\x02h2";
    *data = std::bit_cast<const unsigned char*>(h2.data());
    *len = static_cast<unsigned int>(h2.size());
    return SSL_TLSEXT_ERR_OK;
}

static int alpnSelectProtoCallback(
    SSL* /*unused*/, const unsigned char** out, unsigned char* outlen,
    const unsigned char* in, unsigned int inlen, void* /*unused*/)
{
    int rv = nghttp2_select_alpn(out, outlen, in, inlen);
    if (rv == -1)
    {
        return SSL_TLSEXT_ERR_NOACK;
    }
    if (rv == 1)
    {
        BMCWEB_LOG_DEBUG("Selected HTTP2");
    }
    return SSL_TLSEXT_ERR_OK;
}
static bool getMtlsSslContext(boost::asio::ssl::context& ctx)
{
    constexpr const char* mtlsDir      = "/etc/ssl/certs/mtls";
    constexpr const char* certFile     = "/etc/ssl/certs/mtls/server.crt";
    constexpr const char* keyFile      = "/etc/ssl/certs/mtls/server.key";
    constexpr const char* pemFile      = "/etc/ssl/certs/mtls/server.pem";
    constexpr const char* caFile       = "/etc/ssl/certs/mtls/ca.crt";

    BMCWEB_LOG_CRITICAL("Initializing mTLS SSL context");

    // --- Build PEM at runtime only if missing ---
    if (!std::filesystem::exists(pemFile))
    {
        BMCWEB_LOG_CRITICAL("mTLS combined PEM missing, generating it now");

        if (!std::filesystem::exists(certFile))
        {
            BMCWEB_LOG_ERROR("Missing mTLS server certificate: {}", certFile);
            return false;
        }

        if (!std::filesystem::exists(keyFile))
        {
            BMCWEB_LOG_ERROR("Missing mTLS private key: {}", keyFile);
            return false;
        }

        std::ofstream pem(pemFile);
        if (!pem)
        {
            BMCWEB_LOG_ERROR("Failed to create {}", pemFile);
            return false;
        }

        pem << std::ifstream(certFile).rdbuf();
        pem << std::ifstream(keyFile).rdbuf();
        pem.close();

        chmod(pemFile, 0600);
        if (chown(pemFile, 0, 0) != 0)
        {
            BMCWEB_LOG_ERROR("Failed to chown mtls pemFile:{}", pemFile);
        }
        BMCWEB_LOG_CRITICAL("Generated combined mTLS PEM: {}", pemFile);
    }

    // === TLS Context Configuration ===
    try
    {
        ctx.use_certificate_chain_file("/etc/ssl/certs/mtls/server.pem");
        ctx.use_private_key_file("/etc/ssl/certs/mtls/server.pem", boost::asio::ssl::context::pem);
        BMCWEB_LOG_CRITICAL("Generated combined mTLS PEM: {}", pemFile);
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Failed to load mTLS PEM: {}", e.what());
        return false;
    }

    if (std::filesystem::exists(caFile))
    {
        ctx.load_verify_file(caFile);
        BMCWEB_LOG_CRITICAL("Loaded mTLS CA: {}", caFile);
    }
    else
    {
        BMCWEB_LOG_WARNING("mTLS CA file missing — clients may fail validation");
    }

    if (std::filesystem::exists(mtlsDir))
    {
        ctx.add_verify_path(mtlsDir);
        BMCWEB_LOG_CRITICAL("Added mTLS trust path: {}", mtlsDir);
    }

    ctx.set_verify_mode(boost::asio::ssl::verify_peer /*|
                        boost::asio::ssl::verify_fail_if_no_peer_cert*/);

    SSL_CTX_set_ecdh_auto(ctx.native_handle(), 1);
    SSL_CTX_set_options(ctx.native_handle(), SSL_OP_NO_RENEGOTIATION);

    BMCWEB_LOG_CRITICAL("mTLS SSL context initialized successfully!");

    return true;
}

// Verify callback for certificate-chain validation in mTLS
static int tlsVerifyCallback(int preverifyOk, X509_STORE_CTX* ctx)
{
    BMCWEB_LOG_CRITICAL("tlsVerifyCallback called");
    // If OpenSSL already failed verification → reject immediately
    if (!preverifyOk)
    {
        int err = X509_STORE_CTX_get_error(ctx);
        BMCWEB_LOG_ERROR("mTLS verify failed: ",
                          X509_verify_cert_error_string(err));
        //return 0;
    }

    if (ctx == nullptr)
    {
        BMCWEB_LOG_ERROR("mTLS: Null X509_STORE_CTX");
        return 0;
    }

    // Certificate being verified
    X509* cert = X509_STORE_CTX_get_current_cert(ctx);
    if (!cert)
    {
        BMCWEB_LOG_ERROR("mTLS: No peer certificate");
        return 0;
    }else{
        // Subject
        char *sub = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
        char *iss = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
        if (sub) { BMCWEB_LOG_CRITICAL("mTLS: current cert subject: {}", sub); OPENSSL_free(sub); }
        if (iss) { BMCWEB_LOG_CRITICAL("mTLS: current cert issuer : {}", iss); OPENSSL_free(iss); }

        ASN1_INTEGER* asn1_serial = X509_get_serialNumber(cert);
        if (asn1_serial)
        {
            BIGNUM* bn = ASN1_INTEGER_to_BN(asn1_serial, nullptr);
            if (bn)
            {
                char* hex = BN_bn2hex(bn);
                if (hex) { BMCWEB_LOG_CRITICAL("mTLS: cert serial: {}", hex); OPENSSL_free(hex); }
                BN_free(bn);
            }
        }
    }

    // Optional: Validate CN/SAN
    char cn[256] = {};
    X509_NAME* subject = X509_get_subject_name(cert);

    int ret = X509_NAME_get_text_by_NID(subject, NID_commonName, cn, sizeof(cn));
    if (ret < 0)
    {
        BMCWEB_LOG_ERROR("mTLS: Failed to extract CN");
        return 0;
    }

    std::string commonName(cn);

    // Example policy: Only allow certificates with CN containing "admin"
    if (commonName.find("admin") == std::string::npos)
    {
        BMCWEB_LOG_ERROR("mTLS: CN '{}' is not allowed", commonName);
        return 0;
    }

    BMCWEB_LOG_CRITICAL("mTLS: Peer certificate CN='{}' accepted", commonName);
    return 1; // Accept the certificate
}

// ============================================================================
// - If client presents certificate → mTLS
// - Else if client IP is in allowlist → mTLS
// - Else if SNI == "mtls.bmc" → mTLS
// - Else → HTTPS
// ============================================================================

static int clientHelloCallback(SSL *ssl, int *al, void *arg)
{
    (void)al;
    (void)arg;

    BMCWEB_LOG_CRITICAL("ClientHello callback");

    // ------------------------------------------------------------
    // 1. Detect client IP address
    // ------------------------------------------------------------
    int fd = SSL_get_fd(ssl);
    if (fd < 0)
    {
        BMCWEB_LOG_ERROR( "SSL_get_fd failed");
        *al = SSL_AD_INTERNAL_ERROR;
        //return SSL_CLIENT_HELLO_ERROR;
    }

    sockaddr_storage addr {};
    socklen_t len = sizeof(addr);
    if (getpeername(fd, (sockaddr*)&addr, &len) != 0)
    {
        BMCWEB_LOG_ERROR ("getpeername failed");
        *al = SSL_AD_INTERNAL_ERROR;
        //return SSL_CLIENT_HELLO_ERROR;
    }

    char ipStr[INET6_ADDRSTRLEN];
    if (addr.ss_family == AF_INET)
    {
        auto* a = (sockaddr_in*)&addr;
        inet_ntop(AF_INET, &a->sin_addr, ipStr, sizeof(ipStr));
    }
    else
    {
        auto* a = (sockaddr_in6*)&addr;
        inet_ntop(AF_INET6, &a->sin6_addr, ipStr, sizeof(ipStr));
    }

    BMCWEB_LOG_CRITICAL("Peer IP: ",ipStr);

    // ------------------------------------------------------------
    // 2. Client certificate presence: best & safest mTLS detector
    // ------------------------------------------------------------
    X509 *peer = SSL_get_peer_certificate(ssl);
    if (peer)
    {
        BMCWEB_LOG_CRITICAL("clientHello: client presented certificate → mTLS");

        X509_free(peer);

        if (ensuressl::crow::g_mtlsCtx)
            SSL_set_SSL_CTX(ssl, ensuressl::crow::g_mtlsCtx->native_handle());

        SSL_set_verify(ssl,
                       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       tlsVerifyCallback);
        return SSL_CLIENT_HELLO_SUCCESS;
    }else{
        BMCWEB_LOG_CRITICAL("clientHello: client did not presented certificate Not mtls");
    }

    // ------------------------------------------------------------
    // 3. IP allowlist → treat as mTLS (peer BMC)
    // ------------------------------------------------------------
    static const std::vector<std::string> allowlist = {
        "9.41.166.174",    // your peer BMC
        "9.3.29.238"       // example
    };

    if (std::find(allowlist.begin(), allowlist.end(), std::string(ipStr)) != allowlist.end())
    {
        BMCWEB_LOG_CRITICAL("clientHello: IP {} is allowlisted → mTLS", ipStr);

        if (ensuressl::crow::g_mtlsCtx)
            SSL_set_SSL_CTX(ssl, ensuressl::crow::g_mtlsCtx->native_handle());

        SSL_set_verify(ssl,
                       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       tlsVerifyCallback);
        return SSL_CLIENT_HELLO_SUCCESS;
    }else{
        BMCWEB_LOG_CRITICAL("clientHello: IP {} is NOT in allowlisted → NO mTLS", ipStr);
    }

    // ------------------------------------------------------------
    // 4. SNI fallback (works on OpenSSL 1.1.1 through generic ext parser)
    // ------------------------------------------------------------

    const unsigned char *sni = nullptr;
    size_t sniLen = 0;

    // extType=0 → SNI , we must parse manually because OpenSSL 1.1.1 has no helper API
    if (SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_server_name, &sni, &sniLen) == 1 && sniLen > 5)
    {
        // Parse extension manually:
        // struct {
        //   NameType type;
        //   opaque HostName<1..2^16-1>
        // } ServerName;
        //
        // Format: 2 bytes list length, then entries:
        //
        // | list_len(2) | name_type(1) | host_len(2) | host |
        //

        size_t pos = 2;                     // skip list length
        //uint8_t nameType = sni[pos];        // usually 0 = host_name
        pos++;

        uint16_t hostLen = ((uint16_t)sni[pos] << 8) | sni[pos+1];
        pos += 2;

        if (pos + hostLen <= sniLen)
        {
            std::string hostname((const char *)&sni[pos], hostLen);
            BMCWEB_LOG_INFO("SNI Hostname: {}", hostname);

            if (hostname == "mtls.bmc")
            {
                BMCWEB_LOG_CRITICAL("clientHello: SNI=mtls.bmc → mTLS");

                if (ensuressl::crow::g_mtlsCtx)
                    SSL_set_SSL_CTX(ssl, ensuressl::crow::g_mtlsCtx->native_handle());

                /*SSL_set_verify(ssl,
                               SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                               tlsVerifyCallback);*/
                return SSL_CLIENT_HELLO_SUCCESS;
            }
            else{
                BMCWEB_LOG_CRITICAL("clientHello: SNI not equal mtls.bmc NO MTLS");
            }
        }
    }else{
        BMCWEB_LOG_CRITICAL("clientHello: SNI not found for mtls.bmc → NO mTLS");            
    }

    // ------------------------------------------------------------
    // 5. Default = HTTPS context
    // ------------------------------------------------------------
    BMCWEB_LOG_CRITICAL("clientHello: Using HTTPS context");

    if (ensuressl::crow::g_httpsCtx)
        SSL_set_SSL_CTX(ssl, ensuressl::crow::g_httpsCtx->native_handle());

    SSL_set_verify(ssl, SSL_VERIFY_NONE, tlsVerifyCallback);

    return SSL_CLIENT_HELLO_SUCCESS;
}

static bool getSslContext(boost::asio::ssl::context& mSslContext,
                          const std::string& sslPemFile)
{
    mSslContext.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::single_dh_use |
        boost::asio::ssl::context::no_tlsv1 |
        boost::asio::ssl::context::no_tlsv1_1);
    BMCWEB_LOG_CRITICAL("Using default TrustStore location: {}", trustStorePath);
    mSslContext.add_verify_path(trustStorePath);

    if (!sslPemFile.empty())
    {
        boost::system::error_code ec;

        boost::asio::const_buffer buf(sslPemFile.data(), sslPemFile.size());
        mSslContext.use_certificate_chain(buf, ec);
        if (ec)
        {
            return false;
        }
        mSslContext.use_private_key(buf, boost::asio::ssl::context::pem, ec);
        if (ec)
        {
            BMCWEB_LOG_CRITICAL("Failed to open ssl pkey");
            return false;
        }
    }

    // Set up EC curves to auto (boost asio doesn't have a method for this)
    // There is a pull request to add this.  Once this is included in an asio
    // drop, use the right way
    // http://stackoverflow.com/questions/18929049/boost-asio-with-ecdsa-certificate-issue
    if (SSL_CTX_set_ecdh_auto(mSslContext.native_handle(), 1) != 1)
    {}

    if (SSL_CTX_set_cipher_list(mSslContext.native_handle(),
                                mozillaIntermediate) != 1)
    {
        BMCWEB_LOG_ERROR("Error setting cipher list");
        return false;
    }
    return true;
}

std::shared_ptr<boost::asio::ssl::context> getSslServerContext()
{
    using namespace boost::asio::ssl;
    namespace fs = std::filesystem;

    // Allocate HTTPS context as shared_ptr
    auto httpsCtx = std::make_shared<context>(context::tls_server);

    // Load the HTTPS certificate first
    auto certFile = ensureCertificate();
    if (!getSslContext(*httpsCtx, certFile))
    {
        BMCWEB_LOG_CRITICAL("Couldn't get server context");
        return nullptr;
    }

    SSL_CTX_set_options(httpsCtx->native_handle(), SSL_OP_NO_RENEGOTIATION);
    // Store globally for callbacks
    ensuressl::crow::g_httpsCtx = httpsCtx;

    auto mtlsCtx = std::make_shared<context>(context::tls_server);
    const std::string mtlsCertFile = "/etc/ssl/certs/mtls/server.pem";
    if (!getMtlsSslContext(*mtlsCtx))
    {
        BMCWEB_LOG_CRITICAL("Couldn't load mTLS server certificate, continuing with HTTPS only");
        ensuressl::crow::g_mtlsCtx = nullptr;
    }
    else
    {
        BMCWEB_LOG_CRITICAL("Loaded additional mTLS CA ");
        ensuressl::crow::g_mtlsCtx = mtlsCtx;

        mtlsCtx->set_verify_mode(boost::asio::ssl::verify_peer /*|
                                 boost::asio::ssl::verify_fail_if_no_peer_cert*/);
        ensuressl::crow::g_mtlsCtx = mtlsCtx;
        BMCWEB_LOG_CRITICAL("Loaded mTLS context: {}", mtlsCertFile);
        // Apply same anti-renegotiation option
        SSL_CTX_set_options(ensuressl::crow::g_mtlsCtx->native_handle(), SSL_OP_NO_RENEGOTIATION);
    }
    
    const persistent_data::AuthConfigMethods& c =
        persistent_data::SessionStore::getInstance().getAuthMethodsConfig();

    boost::asio::ssl::verify_mode mode = boost::asio::ssl::verify_none;
    if (c.tlsStrict)
    {

        BMCWEB_LOG_CRITICAL("tlsStrict enabled → HTTPS will convert mtls request client certs");
        if(ensuressl::crow::g_mtlsCtx){    
            ensuressl::crow::g_mtlsCtx->set_verify_mode(boost::asio::ssl::verify_peer /*|
                                  boost::asio::ssl::verify_fail_if_no_peer_cert*/);
            
            if constexpr (BMCWEB_HTTP2)
            {

                SSL_CTX_set_next_protos_advertised_cb(ensuressl::crow::g_mtlsCtx->native_handle(),
                                          nextProtoCallback, nullptr);
                SSL_CTX_set_alpn_select_cb(ensuressl::crow::g_mtlsCtx->native_handle(),
                               alpnSelectProtoCallback, nullptr);
            }
            BMCWEB_LOG_CRITICAL("mtlsCtx created ");
        }
    }
    else if (!forward_unauthorized::hasWebuiRoute())
    {
        // This is a HACK
        // If the webui is installed, and TLSSTrict is false, we don't want to
        // force the mtls popup to occur, which would happen if we requested a
        // client cert by setting verify_peer. But, if the webui isn't
        // installed, we'd like clients to be able to optionally log in with
        // MTLS, which won't happen if we don't expose the MTLS client cert
        // request.  So, in this case detect if the webui is installed, and
        // only request peer authentication if it's not present.
        // This will likely need revisited in the future.
        BMCWEB_LOG_CRITICAL("Setting verify peer only");
        mode |= boost::asio::ssl::verify_peer;
        boost::system::error_code ec;     
        httpsCtx->set_verify_mode(mode, ec);
     if (ec)
     {
        BMCWEB_LOG_CRITICAL("Failed to set verify mode {}", ec.message());
         return nullptr;
     }

    }
    
        // HTTP/2 callbacks (optional)
    // ------------------------

    if constexpr (BMCWEB_HTTP2)
    {
        SSL_CTX_set_next_protos_advertised_cb(httpsCtx->native_handle(),
                                          nextProtoCallback, nullptr);
        SSL_CTX_set_alpn_select_cb(httpsCtx->native_handle(),
                               alpnSelectProtoCallback, nullptr);

    }

    httpsCtx->set_verify_mode(SSL_VERIFY_PEER);

    ensuressl::crow::g_httpsCtx = httpsCtx;
    SSL_CTX* rawHttpsCtx = ensuressl::crow::g_httpsCtx->native_handle(); 
    SSL_CTX_set_tlsext_servername_callback(rawHttpsCtx, sniDummyCallback);    
    SSL_CTX_set_tlsext_servername_arg(rawHttpsCtx,nullptr); 

    //client hello callback for httpscontext
    SSL_CTX_set_client_hello_cb(rawHttpsCtx,
                             clientHelloCallback,
                             nullptr);
    SSL_CTX_set_verify(rawHttpsCtx,
                   SSL_VERIFY_PEER,
                   tlsVerifyCallback);
    if(ensuressl::crow::g_mtlsCtx){
       ensuressl::crow::g_mtlsCtx  = mtlsCtx;
        //client hello callback for mtlscontext 
        SSL_CTX_set_client_hello_cb(ensuressl::crow::g_mtlsCtx->native_handle(),
                             clientHelloCallback,
                             nullptr);
        SSL_CTX_set_verify(ensuressl::crow::g_mtlsCtx->native_handle(), SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, tlsVerifyCallback);
    }
    return httpsCtx;
}


std::optional<boost::asio::ssl::context> getSSLClientContext(
    VerifyCertificate /*verifyCertificate*/)
{
    namespace fs = std::filesystem;

    boost::asio::ssl::context sslCtx(boost::asio::ssl::context::tls_client);

    // NOTE, this path is temporary;  In the future it will need to change to
    // be set per subscription.  Do not rely on this.
    fs::path certPath = "/etc/ssl/certs/https/client.pem";
    std::string cert = verifyOpensslKeyCert(certPath);

    if (!getSslContext(sslCtx, cert))
    {
        return std::nullopt;
    }

    // Currently remote server's certificate verfication fails for
    // HMC provided self-signed certificates,
    // so skip remote server's certificate verfication.

    // TODO: certificate verfication can be enabled after supporting
    // HMC-BMC connection certificate flows on both HMC and BMC.

    // Add a directory containing certificate authority files to be used
    // for performing verification.
    /* boost::system::error_code ec;
    sslCtx.set_default_verify_paths(ec);
    if (ec)
    {
        BMCWEB_LOG_ERROR("SSL context set_default_verify failed");
        return std::nullopt;
    }

    int mode = boost::asio::ssl::verify_peer;
    if (verifyCertificate == VerifyCertificate::NoVerify)
    {
        mode = boost::asio::ssl::verify_none;
    }

    // Verify the remote server's certificate
    sslCtx.set_verify_mode(mode, ec);
    if (ec)
    {
        BMCWEB_LOG_ERROR("SSL context set_verify_mode failed");
        return std::nullopt;
    } */

    if (SSL_CTX_set_cipher_list(sslCtx.native_handle(), mozillaIntermediate) !=
        1)
    {
        BMCWEB_LOG_ERROR("SSL_CTX_set_cipher_list failed");
        return std::nullopt;
    }

    return {std::move(sslCtx)};
}

} // namespace ensuressl
