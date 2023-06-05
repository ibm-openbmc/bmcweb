#include "audit_events.hpp"

#include <gtest/gtest.h> // IWYU pragma: keep

// IWYU pragma: no_include <gtest/gtest-message.h>
// IWYU pragma: no_include <gtest/gtest-test-part.h>
// IWYU pragma: no_include "gtest/gtest_pred_impl.h"

namespace audit
{
namespace
{

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
    auditClose(true);
    EXPECT_TRUE(tryOpen);
    EXPECT_EQ(auditfd, -1);

    EXPECT_TRUE(auditOpen());
    auditClose(true);
    EXPECT_TRUE(tryOpen);
    EXPECT_EQ(auditfd, -1);

    EXPECT_TRUE(auditOpen());
    auditClose(false);
    EXPECT_FALSE(tryOpen);
    EXPECT_EQ(auditfd, -1);
}

} // namespace
} // namespace audit
