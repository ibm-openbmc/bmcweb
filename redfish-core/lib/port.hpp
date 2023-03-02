#pragma once

#include <dbus_utility.hpp>
#include <utils/json_utils.hpp>

namespace redfish
{

inline void getPortLocation(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& serviceName,
                            const std::string& portPath)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, serviceName, portPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [aResp](const boost::system::error_code ec,
                const std::string& property) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR << "DBUS response error for Location";
                messages::internalError(aResp->res);
            }
            return;
        }

        aResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
            property;
        });
}

inline void
    onMapperSubtreeDone(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& portID, const std::string& adapterID,
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree " << ec;
        messages::internalError(asyncResp->res);
        return;
    }

    if (subtree.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Port", portID);
        return;
    }

    for (const auto& [objectPath, serviceMap] : subtree)
    {
        std::string parentAdapter =
            sdbusplus::message::object_path(objectPath).parent_path();
        parentAdapter =
            sdbusplus::message::object_path(parentAdapter).filename();

        BMCWEB_LOG_ERROR << "parentAdapter " << parentAdapter;
        BMCWEB_LOG_ERROR << "Port ID " << portID;

        if (parentAdapter == adapterID)
        {
            std::string portName =
                sdbusplus::message::object_path(objectPath).filename();
            BMCWEB_LOG_ERROR << "portName" << portName;

            if (portName != portID)
            {
                // not the port we are interested in
                continue;
            }

            asyncResp->res.addHeader(boost::beast::http::field::link,
                                     "</redfish/v1/JsonSchemas/port/"
                                     "Port.json>; rel=describedby");

            asyncResp->res.jsonValue["@odata.id"] =
                crow::utility::urlFromPieces("redfish", "v1", "Systems",
                                             "systems", "FabricAdapters",
                                             adapterID, "Ports", portID);

            asyncResp->res.jsonValue["@odata.type"] = "#Port.v1_3_0.Port";
            asyncResp->res.jsonValue["Id"] = portID;
            asyncResp->res.jsonValue["Name"] = portID;

            getPortLocation(asyncResp, serviceMap.begin()->first, objectPath);
            return;
        }
    }
    messages::resourceNotFound(asyncResp->res, "Port", portID);
    return;
}

inline void handlePortGet(crow::App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& adapterId,
                          const std::string& portId)
{
    BMCWEB_LOG_DEBUG << "Get port = " << portId
                     << " on adapter = " << adapterId;

    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Inventory.Item.Connector"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, portId,
         adapterId](const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        onMapperSubtreeDone(asyncResp, portId, adapterId, ec, subtree);
        });
}

inline void onMapperSubtreeDoneForCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& adapterID, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR << "D-Bus response error on GetSubTree " << ec;
        messages::internalError(asyncResp->res);
        return;
    }
    if (subtree.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Adapter", adapterID);
        return;
    }

    for (const auto& [objectPath, serviceMap] : subtree)
    {
        std::string adapter =
            sdbusplus::message::object_path(objectPath).filename();

        if (adapter.empty())
        {
            // invalid object path, continue
            continue;
        }

        if (adapterID != adapter)
        {
            // this is not the adapter we are interested in
            continue;
        }

        // imples adapterId was found, it is a valid
        // adapterId Util collection api will give all the
        // ports implementing this interface but we are only
        // interested in subset of those ports. Ports
        // attached to the given fabric adapter.
        constexpr std::array<std::string_view, 1> interfaces{
            "xyz.openbmc_project.Inventory.Item.Connector"};

        collection_util::getCollectionMembers(
            asyncResp,
            boost::urls::url(crow::utility::urlFromPieces(
                "redfish", "v1", "Systems", "system", "FabricAdapters",
                adapterID, "Ports")),
            interfaces, objectPath.c_str());
        return;
    }
    BMCWEB_LOG_ERROR << "Adapter not found";
    messages::resourceNotFound(asyncResp->res, "FabricAdapter", adapterID);
    return;
}

inline void
    handlePortCollectionGet(crow::App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& adapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.addHeader(boost::beast::http::field::link,
                             "</redfish/v1/JsonSchemas/PortCollection/"
                             "PortCollection.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] = "#PortCollection.PortCollection";
    asyncResp->res.jsonValue["Name"] = "Port Collection";
    asyncResp->res.jsonValue["@odata.id"] =
        crow::utility::urlFromPieces("redfish", "v1", "Systems", "system",
                                     "FabricAdapters", adapterId, "Ports");

    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Inventory.Item.FabricAdapter"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp,
         adapterId](const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        onMapperSubtreeDoneForCollection(asyncResp, adapterId, ec, subtree);
        });
}

inline void requestRoutesPortCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/FabricAdapters/<str>/Ports/")
        .privileges(redfish::privileges::getPort)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortCollectionGet, std::ref(app)));
}

/**
 * Systems derived class for delivering port Schema.
 */
inline void requestRoutesPort(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/system/FabricAdapters/<str>/Ports/<str>/")
        .privileges(redfish::privileges::getPort)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortGet, std::ref(app)));
}

} // namespace redfish