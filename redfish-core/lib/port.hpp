#pragma once

#include "app.hpp"
#include "fabric_adapters.hpp"
#include "led.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"

#include <utils/fabric_util.hpp>
#include <utils/json_utils.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace redfish
{

/**
 * @brief Api to get Port properties.
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       portInvPath Object path of the Port.
 * @param[in]       serviceMap  A map to hold Service and corresponding
 *                              interface list for the given port.
 */
inline void getPortProperties(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                              const std::string& portInvPath,
                              const dbus::utility::MapperServiceMap& serviceMap)
{
    BMCWEB_LOG_DEBUG << "Getting Properties for port " << portInvPath;

    for (const auto& connection : serviceMap)
    {
        for (const auto& interface : connection.second)
        {
            if (interface ==
                "xyz.openbmc_project.Inventory.Decorator.LocationCode")
            {
                sdbusplus::asio::getProperty<std::string>(
                    *crow::connections::systemBus, connection.first,
                    portInvPath, interface, "LocationCode",
                    [aResp](const boost::system::error_code& ec,
                            const std::string& value) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG << "DBUS response error";
                        messages::internalError(aResp->res);
                        return;
                    }

                    aResp->res.jsonValue["Location"]["PartLocation"]
                                        ["ServiceLabel"] = value;
                });
            }
            else if (interface == "xyz.openbmc_project.Association.Definitions")
            {
                getLocationIndicatorActive(aResp, portInvPath);
            }
        }
    }
}

/**
 * @brief Api to get collection of FRUs to be modelled as
 * Port.
 *
 * @param[in,out]   aResp              Async HTTP response.
 * @param[in]       systemName         System name Id.
 * @param[in]       adapterId          AdapterId whose ports
 * are to be collected.
 * @param[in]       interfaces         List of interfaces to
 * constrain the GetSubTree search
 */
inline void getPortCollection(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                              const std::string& systemName,
                              const std::string& adapterId)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Connector"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [aResp, systemName,
         adapterId](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreePathsResponse& paths) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG << "DBUS response error";
            messages::internalError(aResp->res);
            return;
        }
        nlohmann::json& members = aResp->res.jsonValue["Members"];
        members = nlohmann::json::array();

        for (const auto& path : paths)
        {
            // get adapterPath from the dbus connector path
            std::string adapterPath =
                sdbusplus::message::object_path(path).parent_path();
            const std::string& adapterUniq =
                fabric_util::buildFabricUniquePath(adapterPath);
            if (!fabric_util::checkFabricAdapterId(adapterId, adapterUniq))
            {
                continue;
            }

            std::string connectorId =
                sdbusplus::message::object_path(path).filename();
            nlohmann::json item;
            item["@odata.id"] = crow::utility::urlFromPieces(
                "redfish", "v1", "Systems", systemName, "FabricAdapters",
                adapterId, "Ports", connectorId);
            members.emplace_back(std::move(item));
        }
        aResp->res.jsonValue["Members@odata.count"] = members.size();
    });
}

inline void
    handlePortCollectionHead(crow::App& app, const crow::Request& req,
                             const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& systemName,
                             const std::string& adapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    getValidFabricAdapterPath(adapterId, systemName, aResp,
                              [aResp](const std::string&, const std::string&,
                                      const dbus::utility::InterfaceList&) {
        aResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/PortCollection/PortCollection.json>; rel=describedby");
    });
}

inline void doPortCollectionGet(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& systemName,
                                const std::string& adapterId)
{
    aResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/PortCollection/PortCollection.json>; rel=describedby");
    aResp->res.jsonValue["@odata.type"] = "#PortCollection.PortCollection";
    aResp->res.jsonValue["Name"] = "Port Collection";
    aResp->res.jsonValue["@odata.id"] =
        crow::utility::urlFromPieces("redfish", "v1", "Systems", systemName,
                                     "FabricAdapters", adapterId, "Ports");

    getPortCollection(aResp, systemName, adapterId);
}

inline void
    handlePortCollectionGet(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& systemName,
                            const std::string& adapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    getValidFabricAdapterPath(
        adapterId, systemName, aResp,
        [aResp, systemName, adapterId](const std::string&, const std::string&,
                                       const dbus::utility::InterfaceList&) {
        doPortCollectionGet(aResp, systemName, adapterId);
    });
}

/**
 * Systems derived class for delivering port collection Schema.
 */
inline void requestRoutesPortCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/FabricAdapters/<str>/Ports/")
        .privileges(redfish::privileges::headPortCollection)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handlePortCollectionHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/FabricAdapters/<str>/Ports/")
        .privileges(redfish::privileges::getPortCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortCollectionGet, std::ref(app)));
}

inline void handlePortError(const boost::system::error_code& ec,
                            crow::Response& res, const std::string& portId)
{
    if (ec.value() == boost::system::errc::io_error)
    {
        messages::resourceNotFound(res, "#Port.v1_7_0.Port", portId);
        return;
    }

    BMCWEB_LOG_ERROR << "DBus method call failed with error " << ec.value();
    messages::internalError(res);
}

inline bool checkPortId(const std::string& portPath, const std::string& portId)
{
    std::string portName = sdbusplus::message::object_path(portPath).filename();

    return !(portName.empty() || portName != portId);
}

inline void getValidPortPath(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& adapterPath, const std::string& portId,
    std::function<void(const std::string& portPath,
                       const dbus::utility::MapperServiceMap& serviceMap)>&&
        callback)
{
    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Inventory.Item.Connector"};

    dbus::utility::getSubTree(
        adapterPath, 0, interfaces,
        [portId, aResp,
         callback](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            handlePortError(ec, aResp->res, portId);
            return;
        }
        for (const auto& [portPath, serviceMap] : subtree)
        {
            if (checkPortId(portPath, portId))
            {
                callback(portPath, serviceMap);
                return;
            }
        }
        BMCWEB_LOG_WARNING << "Port not found";
        messages::resourceNotFound(aResp->res, "Port", portId);
    });
}

inline void handlePortHead(crow::App& app, const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& systemName,
                           const std::string& adapterId,
                           const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    getValidFabricAdapterPath(
        adapterId, systemName, aResp,
        [aResp, portId](const std::string& adapterPath, const std::string&,
                        const dbus::utility::InterfaceList&) {
        getValidPortPath(aResp, adapterPath, portId,
                         [aResp](const std::string&,
                                 const dbus::utility::MapperServiceMap&) {
            aResp->res.addHeader(
                boost::beast::http::field::link,
                "</redfish/v1/JsonSchemas/Port/Port.json>; rel=describedby");
        });
    });
}

inline void handlePortGet(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const std::string& systemName,
                          const std::string& adapterId,
                          const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    getValidFabricAdapterPath(
        adapterId, systemName, aResp,
        [aResp, portId, adapterId,
         systemName](const std::string& adapterPath, const std::string&,
                     const dbus::utility::InterfaceList&) {
        getValidPortPath(
            aResp, adapterPath, portId,
            [aResp, adapterId, systemName,
             portId](const std::string& portPath,
                     const dbus::utility::MapperServiceMap& serviceMap) {
            aResp->res.addHeader(
                boost::beast::http::field::link,
                "</redfish/v1/JsonSchemas/Port/Port.json>; rel=describedby");

            aResp->res.jsonValue["@odata.type"] = "#Port.v1_7_0.Port";
            aResp->res.jsonValue["@odata.id"] = crow::utility::urlFromPieces(
                "redfish", "v1", "Systems", systemName, "FabricAdapters",
                adapterId, "Ports", portId);
            aResp->res.jsonValue["Id"] = portId;
            aResp->res.jsonValue["Name"] = portId;

            getPortProperties(aResp, portPath, serviceMap);
        });
    });
}

inline void
    setPortProperties(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                      const std::string& portInvPath,
                      const dbus::utility::MapperServiceMap& serviceMap,
                      const std::optional<bool>& locationIndicatorActive)
{
    for (const auto& connection : serviceMap)
    {
        for (const auto& interface : connection.second)
        {
            if (interface ==
                    "xyz.openbmc_project.Inventory.Decorator.LocationCode" &&
                locationIndicatorActive)
            {
                setLocationIndicatorActive(aResp, portInvPath,
                                           *locationIndicatorActive);
            }
        }
    }
}

inline void handlePortPatch(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& systemName,
                            const std::string& adapterId,
                            const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    std::optional<bool> locationIndicatorActive;
    if (!json_util::readJsonPatch(req, aResp->res, "LocationIndicatorActive",
                                  locationIndicatorActive))
    {
        return;
    }

    getValidFabricAdapterPath(adapterId, systemName, aResp,
                              [aResp, portId, locationIndicatorActive](
                                  const std::string& adapterPath,
                                  const std::string&,
                                  const dbus::utility::InterfaceList&) {
        getValidPortPath(
            aResp, adapterPath, portId,
            [aResp, locationIndicatorActive](
                const std::string& portPath,
                const dbus::utility::MapperServiceMap& serviceMap) {
            setPortProperties(aResp, portPath, serviceMap,
                              locationIndicatorActive);
        });
    });
}

/**
 * Systems derived class for delivering port Schema.
 */
inline void requestRoutesPort(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/FabricAdapters/<str>/Ports/<str>/")
        .privileges(redfish::privileges::headPort)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handlePortHead, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/FabricAdapters/<str>/Ports/<str>/")
        .privileges(redfish::privileges::getPort)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortGet, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/FabricAdapters/<str>/Ports/<str>/")
        .privileges(redfish::privileges::patchPort)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handlePortPatch, std::ref(app)));
}

} // namespace redfish
