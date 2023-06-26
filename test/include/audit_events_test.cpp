#include "audit_events.hpp"

#include <gtest/gtest.h> // IWYU pragma: keep

// IWYU pragma: no_include <gtest/gtest-message.h>
// IWYU pragma: no_include <gtest/gtest-test-part.h>
// IWYU pragma: no_include "gtest/gtest_pred_impl.h"

namespace audit
{
namespace
{

TEST(auditSetState, PositiveTest)
{
    auditSetState(false);
    EXPECT_FALSE(auditOpen());

    auditSetState(true);
    EXPECT_TRUE(auditOpen());
    auditClose();
}

TEST(auditOpen, PositiveTest)
{
    int origFd;

    EXPECT_TRUE(auditOpen());
    EXPECT_NE(auditfd, -1);

    origFd = auditfd;
    EXPECT_TRUE(auditOpen());
    EXPECT_EQ(auditfd, origFd);
}

TEST(auditClose, PositiveTest)
{
    auditClose();
    EXPECT_EQ(auditfd, -1);

    EXPECT_TRUE(auditOpen());
    auditClose();
    EXPECT_EQ(auditfd, -1);
}

TEST(auditReopen, PositiveTest)
{
    EXPECT_TRUE(auditReopen());
    EXPECT_NE(auditfd, -1);

    // Cannot make expectation on different fd on reopen
    EXPECT_TRUE(auditReopen());
    EXPECT_NE(auditfd, -1);

    auditClose();
    EXPECT_TRUE(auditReopen());
    EXPECT_NE(auditfd, -1);
}

TEST(wantAudit, PositiveTest)
{
    constexpr const std::string_view url = "/foo";
    std::error_code ec;

    crow::Request patchRequest{{boost::beast::http::verb::patch, url, 11}, ec};
    EXPECT_TRUE(wantAudit(patchRequest));

    crow::Request putRequest{{boost::beast::http::verb::put, url, 11}, ec};
    EXPECT_TRUE(wantAudit(putRequest));

    crow::Request deleteRequest{{boost::beast::http::verb::delete_, url, 11},
                                ec};
    EXPECT_TRUE(wantAudit(deleteRequest));

    crow::Request postRequest{{boost::beast::http::verb::post, url, 11}, ec};
    EXPECT_TRUE(wantAudit(postRequest));
}

TEST(wantAudit, NegativeTest)
{
    constexpr const std::string_view url = "/foo";
    std::error_code ec;

    crow::Request postRequest{{boost::beast::http::verb::post, url, 11}, ec};

    postRequest.target("/redfish/v1/SessionService/Sessions");
    EXPECT_FALSE(wantAudit(postRequest));

    postRequest.target("/redfish/v1/SessionService/Sessions/");
    EXPECT_FALSE(wantAudit(postRequest));

    postRequest.target("/login");
    EXPECT_FALSE(wantAudit(postRequest));

    crow::Request getRequest{{boost::beast::http::verb::get, url, 11}, ec};
    EXPECT_FALSE(wantAudit(getRequest));
}

} // namespace
} // namespace audit
