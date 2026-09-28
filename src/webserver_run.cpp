// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "webserver_run.hpp"

#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_monitor.hpp"
#include "dbus_singleton.hpp"
#include "event_service_manager.hpp"
#include "google_service_root.hpp"
#include "hostname_monitor.hpp"
#include "ibm_locks.hpp"
#include "ibm_management_console_rest.hpp"
#include "image_upload.hpp"
#include "io_context_singleton.hpp"
#include "kvm_websocket.hpp"
#include "logging.hpp"
#include "login_routes.hpp"
#include "obmc_console.hpp"
#include "obmc_hypervisor.hpp"
#include "obmc_shell.hpp"
#include "openbmc_dbus_rest.hpp"
#include "persistent_data.hpp"
#include "redfish.hpp"
#include "redfish_aggregator.hpp"
#include "ssl_context_factory_sni.hpp"
#include "user_monitor.hpp"
#include "vm_websocket.hpp"
#include "watchdog.hpp"
#include "webassets.hpp"

#include <boost/asio/io_context.hpp>
#include <event_dbus_monitor.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

static void setLogLevel(const std::string& logLevel)
{
    const std::basic_string_view<char>* iter =
        std::ranges::find(crow::mapLogLevelFromName, logLevel);
    if (iter == crow::mapLogLevelFromName.end())
    {
        BMCWEB_LOG_ERROR("log-level {} not found", logLevel);
        return;
    }
    crow::getBmcwebCurrentLoggingLevel() = crow::getLogLevelFromName(logLevel);
    BMCWEB_LOG_INFO("Requested log-level change to: {}", logLevel);
}

int run()
{
    boost::asio::io_context& io = getIoContext();
    App app;

    std::shared_ptr<sdbusplus::asio::connection> systemBus =
        std::make_shared<sdbusplus::asio::connection>(io);
    crow::connections::systemBus = systemBus.get();

    auto server = sdbusplus::asio::object_server(systemBus);

    std::shared_ptr<sdbusplus::asio::dbus_interface> iface =
        server.add_interface("/xyz/openbmc_project/bmcweb",
                             "xyz.openbmc_project.bmcweb");

    iface->register_method("SetLogLevel", setLogLevel);

    iface->initialize();
    // Load the peristent data
    persistent_data::getConfig();

    // Static assets need to be initialized before Authorization, because auth
    // needs to build the whitelist from the static routes

    if constexpr (BMCWEB_STATIC_HOSTING)
    {
        crow::webassets::requestRoutes(app);
    }

    if constexpr (BMCWEB_KVM)
    {
        crow::obmc_kvm::requestRoutes(app);
    }

    if constexpr (BMCWEB_REDFISH)
    {
        redfish::RedfishService::getInstance(app);

        // Create EventServiceManager instance and initialize Config
        redfish::EventServiceManager::getInstance();

        if constexpr (BMCWEB_REDFISH_AGGREGATION)
        {
            // Create RedfishAggregator instance and initialize Config
            redfish::RedfishAggregator::getInstance();
        }
    }

    if constexpr (BMCWEB_REST)
    {
        crow::image_upload::requestRoutes(app);
        crow::openbmc_mapper::requestRoutes(app);
    }

    if constexpr (BMCWEB_EVENT_SUBSCRIPTION)
    {
        crow::dbus_monitor::requestRoutes(app);
    }

    if constexpr (BMCWEB_HOST_SERIAL_SOCKET)
    {
        crow::obmc_console::requestRoutes(app);
    }

    if constexpr (BMCWEB_HYPERVISOR_SERIAL_SOCKET)
    {
        crow::obmc_hypervisor::requestRoutes(app);
    }

    if constexpr (BMCWEB_BMC_SHELL_SOCKET)
    {
        crow::obmc_shell::requestRoutes(app);
    }

    crow::obmc_vm::requestRoutes(app);

    if constexpr (BMCWEB_IBM_MANAGEMENT_CONSOLE)
    {
        crow::ibm_mc::requestRoutes(app);
        crow::ibm_mc_lock::Lock::getInstance();
        // Start BMC and Host state change dbus monitor
        crow::dbus_monitor::registerStateChangeSignal();
        // Start Dump created signal monitor for BMC and System Dump
        crow::dbus_monitor::registerDumpUpdateSignal();
        // Start BIOS Attr change dbus monitor
        crow::dbus_monitor::registerBIOSAttrUpdateSignal();
        // Start event log entry created monitor
        crow::dbus_monitor::registerEventLogCreatedSignal();
        // Start PostCode change signal
        crow::dbus_monitor::registerPostCodeChangeSignal();
        // Start hypervisor app dbus monitor for hypervisor
        // network configurations
        crow::dbus_monitor::registerVMIConfigChangeSignal();
        // Start Platform and Partition SAI state change monitor
        crow::dbus_monitor::registerSAIStateChangeSignal();
    }

    if constexpr (BMCWEB_GOOGLE_API)
    {
        crow::google_api::requestRoutes(app);
    }

    crow::login_routes::requestRoutes(app);

    if constexpr (!BMCWEB_INSECURE_DISABLE_SSL)
    {
        BMCWEB_LOG_INFO("Start Hostname Monitor Service...");
        crow::hostname_monitor::registerHostnameSignal();
    }

    bmcweb::registerUserRemovedSignal();

    bmcweb::ServiceWatchdog watchdog;
    // mTLS server key location: when uri-key is configured the key lives in a
    // provider (e.g. a TPM handle:) and is loaded via OSSL_STORE; otherwise
    // fall back to the filesystem PEM.
    std::string mtlsServerKey = "/etc/ssl/private/server_pkey.pem";
    if constexpr (!BMCWEB_URI_KEY.empty())
    {
        mtlsServerKey = BMCWEB_URI_KEY;
    }
    // mTLS server cert location: uri-cert overrides the default path when
    // configured. A provider URI (e.g. a TPM NV "handle:") is passed
    // through verbatim so the SNI factory loads it via OSSL_STORE; a
    // file:// URI is resolved to a filesystem path.
    std::string mtlsServerCert = "/etc/ssl/certs/https/server_cert.pem";
    if constexpr (!BMCWEB_URI_CERT.empty())
    {
        if (ensuressl::isProviderCert(BMCWEB_URI_CERT))
        {
            mtlsServerCert = BMCWEB_URI_CERT;
        }
        else
        {
            std::optional<std::string> resolved =
                ensuressl::fileUriToPath(BMCWEB_URI_CERT);
            if (resolved)
            {
                mtlsServerCert = *resolved;
            }
            else
            {
                BMCWEB_LOG_ERROR(
                    "Unsupported uri-cert {} (file:// or handle: only); using default {}",
                    BMCWEB_URI_CERT, mtlsServerCert);
            }
        }
    }
    bmcweb::SniContextFactoryState state(
        [](const std::string& sniname) {
            return sniname.starts_with("9.6.28.10");
        },
        mtlsServerCert, mtlsServerKey, "/etc/ssl/certs/authority");
    app.run(state);

    systemBus->request_name("xyz.openbmc_project.bmcweb");

    io.run();

    crow::connections::systemBus = nullptr;

    return 0;
}
