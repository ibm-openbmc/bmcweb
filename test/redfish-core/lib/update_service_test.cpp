// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "async_resp.hpp"
#include "generated/enums/oem_update_service.hpp"
#include "http_response.hpp"
#include "update_service.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>

#include <cerrno>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

TEST(UpdateService, ParseHTTSPPostitive)
{
    crow::Response res;
    {
        // No protocol, schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", std::nullopt, res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Protocol, no schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
}

TEST(UpdateService, ParseHTTPSPostitive)
{
    crow::Response res;
    {
        // No protocol, schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", std::nullopt, res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Protocol, no schema on url
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url with path
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1/path", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/path");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url without path
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://1.1.1.1", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "1.1.1.1");
        EXPECT_EQ(ret->encoded_path(), "/");
        EXPECT_EQ(ret->scheme(), "https");
    }
    {
        // Both protocol and schema on url without path
        std::optional<boost::urls::url> ret =
            parseSimpleUpdateUrl("https://[2001:db8::1]", "HTTPS", res);
        ASSERT_TRUE(ret);
        if (!ret)
        {
            return;
        }
        EXPECT_EQ(ret->encoded_host_and_port(), "[2001:db8::1]");
        EXPECT_EQ(ret->encoded_path(), "/");
        EXPECT_EQ(ret->scheme(), "https");
    }
}

TEST(UpdateService, ParseHTTPSNegative)
{
    crow::Response res;
    // No protocol, no schema
    ASSERT_EQ(parseSimpleUpdateUrl("1.1.1.1/path", std::nullopt, res),
              std::nullopt);
    // No host
    ASSERT_EQ(parseSimpleUpdateUrl("/path", "HTTPS", res), std::nullopt);
}

TEST(DbusBootSideToRf, DbusToRf)
{
    EXPECT_EQ(dbusBootSideToRf(
                  "xyz.openbmc_project.Software.BootSide.BootSides.Temp"),
              oem_update_service::BootSide::Temp);
    EXPECT_EQ(dbusBootSideToRf(
                  "xyz.openbmc_project.Software.BootSide.BootSides.Perm"),
              oem_update_service::BootSide::Perm);
}

TEST(DbusBootSideToRf, ElseResultsIntoUnknown)
{
    EXPECT_EQ(dbusBootSideToRf(""), oem_update_service::BootSide::Unknown);
    EXPECT_EQ(dbusBootSideToRf(
                  "xyz.openbmc_project.Software.BootSide.BootSides.Unknown"),
              oem_update_service::BootSide::Unknown);
    EXPECT_EQ(dbusBootSideToRf("Random"),
              oem_update_service::BootSide::Unknown);
}

TEST(RfBootSideToDbus, RfToDbus)
{
    EXPECT_EQ(rfBootSideToDbus("Temp"),
              "xyz.openbmc_project.Software.BootSide.BootSides.Temp");
    EXPECT_EQ(rfBootSideToDbus("Perm"),
              "xyz.openbmc_project.Software.BootSide.BootSides.Perm");
}

TEST(RfBootSideToDbus, ElseResultsIntoNullopt)
{
    EXPECT_EQ(rfBootSideToDbus(""), std::nullopt);
    EXPECT_EQ(rfBootSideToDbus("Unknown"), std::nullopt);
    EXPECT_EQ(rfBootSideToDbus("Random"), std::nullopt);
}

TEST(AfterGetBootSideInfo, BootSidesAreWritten)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties;
    properties.emplace_back(
        "CurrentBootSide",
        dbus::utility::DbusVariantType(
            "xyz.openbmc_project.Software.BootSide.BootSides.Temp"));
    properties.emplace_back(
        "NextBootSide",
        dbus::utility::DbusVariantType(
            "xyz.openbmc_project.Software.BootSide.BootSides.Perm"));

    afterGetBootSideInfo(asyncResp, boost::system::error_code(), properties);

    EXPECT_EQ(asyncResp->res.jsonValue["Oem"]["IBM"]["CurrentBootSide"],
              oem_update_service::BootSide::Temp);
    EXPECT_EQ(asyncResp->res.jsonValue["Oem"]["IBM"]["NextBootSide"],
              oem_update_service::BootSide::Perm);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

TEST(AfterGetBootSideInfo, UnrecognizedValuesResultInUnknown)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties;
    properties.emplace_back(
        "CurrentBootSide",
        dbus::utility::DbusVariantType(
            "xyz.openbmc_project.Software.BootSide.BootSides.Unknown"));
    properties.emplace_back("NextBootSide",
                            dbus::utility::DbusVariantType("Random"));

    afterGetBootSideInfo(asyncResp, boost::system::error_code(), properties);

    EXPECT_EQ(asyncResp->res.jsonValue["Oem"]["IBM"]["CurrentBootSide"],
              oem_update_service::BootSide::Unknown);
    EXPECT_EQ(asyncResp->res.jsonValue["Oem"]["IBM"]["NextBootSide"],
              oem_update_service::BootSide::Unknown);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

TEST(AfterGetBootSideInfo, MissingPropertiesAreNotWritten)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties;
    properties.emplace_back(
        "NextBootSide",
        dbus::utility::DbusVariantType(
            "xyz.openbmc_project.Software.BootSide.BootSides.Temp"));

    afterGetBootSideInfo(asyncResp, boost::system::error_code(), properties);

    EXPECT_FALSE(
        asyncResp->res.jsonValue["Oem"]["IBM"].contains("CurrentBootSide"));
    EXPECT_EQ(asyncResp->res.jsonValue["Oem"]["IBM"]["NextBootSide"],
              oem_update_service::BootSide::Temp);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

TEST(AfterGetBootSideInfo, EmptyPropertiesWriteNothing)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties;

    afterGetBootSideInfo(asyncResp, boost::system::error_code(), properties);

    EXPECT_FALSE(
        asyncResp->res.jsonValue["Oem"]["IBM"].contains("CurrentBootSide"));
    EXPECT_FALSE(
        asyncResp->res.jsonValue["Oem"]["IBM"].contains("NextBootSide"));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

TEST(AfterGetBootSideInfo, DbusErrorResultsInInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties;

    afterGetBootSideInfo(
        asyncResp,
        boost::system::error_code(EIO, boost::system::generic_category()),
        properties);

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(AfterGetBootSideInfo, InterfaceNotPresentIsIgnored)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties;

    afterGetBootSideInfo(
        asyncResp,
        boost::system::error_code(EBADR, boost::system::generic_category()),
        properties);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_FALSE(asyncResp->res.jsonValue.contains("Oem"));
}
} // namespace
} // namespace redfish
