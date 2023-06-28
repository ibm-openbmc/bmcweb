#pragma once

#include "async_resp.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "utility.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/system/error_code.hpp>
#include <dbus_singleton.hpp>
#include <dbus_utility.hpp>

#include <cstddef>
#include <string>

namespace redfish
{
namespace dump_utils
{

inline void getValidDumpEntryForAttachment(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& url,
    std::function<void(const std::string& objectPath,
                       const std::string& entryID,
                       const std::string& dumpType)>&& callback)
{
    std::string dumpType;
    std::string entryID;
    std::string entriesPath;

    if (crow::utility::readUrlSegments(url, "redfish", "v1", "Managers", "bmc",
                                       "LogServices", "Dump", "Entries",
                                       std::ref(entryID), "attachment"))
    {
        // BMC type dump
        dumpType = "BMC";
        entriesPath = "/redfish/v1/Managers/bmc/LogServices/Dump/Entries/";
    }
    else if (crow::utility::readUrlSegments(
                 url, "redfish", "v1", "Systems", "system", "LogServices",
                 "Dump", "Entries", std::ref(entryID), "attachment"))
    {
        // System type
        dumpType = "System";
        entriesPath = "/redfish/v1/Systems/system/LogServices/Dump/Entries/";
    }
    if (dumpType.empty() || entryID.empty())
    {
        redfish::messages::resourceNotFound(asyncResp->res, "Dump", entryID);
        return;
    }

    std::string dumpId(entryID);

    if (dumpType != "BMC")
    {
        std::size_t pos = entryID.find_first_of('_');
        if (pos == std::string::npos || (pos + 1) >= entryID.length())
        {
            // Requested ID is invalid
            messages::invalidObject(
                asyncResp->res, crow::utility::urlFromPieces(
                                    "redfish", "v1", "Systems", "system",
                                    "LogServices", "Dump", "Entries", entryID));
            return;
        }
        dumpId = entryID.substr(pos + 1);
    }

    auto getValidDumpEntryCallback =
        [asyncResp, entryID, dumpType, dumpId, entriesPath, callback](

            const boost::system::error_code& ec,
            const dbus::utility::ManagedObjectType& resp) {
        if (ec.value() == EBADR)
        {
            messages::resourceNotFound(asyncResp->res, dumpType + " dump",
                                       entryID);
            return;
        }
        if (ec)
        {
            BMCWEB_LOG_ERROR << "DumpEntry resp_handler got error " << ec;
            messages::internalError(asyncResp->res);
            return;
        }

        std::string dumpEntryIdPath =
            "/xyz/openbmc_project/dump/" +
            std::string(boost::algorithm::to_lower_copy(dumpType)) + "/entry/" +
            dumpId;

        for (const auto& objectPath : resp)
        {
            if (objectPath.first.str != dumpEntryIdPath)
            {
                continue;
            }
            callback(std::move(dumpEntryIdPath), entryID, dumpType);
            return;
        }

        BMCWEB_LOG_WARNING << "Can't find Dump Entry " << entryID;
        messages::resourceNotFound(asyncResp->res, dumpType + " dump", entryID);
    };

    crow::connections::systemBus->async_method_call(
        std::move(getValidDumpEntryCallback),
        "xyz.openbmc_project.Dump.Manager", "/xyz/openbmc_project/dump",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
}

} // namespace dump_utils
} // namespace redfish
