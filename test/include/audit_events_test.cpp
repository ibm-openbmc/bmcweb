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

} // namespace
} // namespace audit
