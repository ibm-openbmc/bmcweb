// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "http_response.hpp"
#include "systems.hpp"
#include "update_service.hpp"

#include <boost/url/url.hpp>

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

TEST(RfBootSideToDbus, ElseResultsIntoEmptyString)
{
    EXPECT_EQ(rfBootSideToDbus(""), "");
    EXPECT_EQ(rfBootSideToDbus("Unknown"), "");
    EXPECT_EQ(rfBootSideToDbus("Random"), "");
}
} // namespace
} // namespace redfish
