#include "bridge_report/runtime/BuildInfo.hpp"
#include <gtest/gtest.h>
TEST(BuildInfoTest, EmbeddedIdentityContainsActualBuildInputs) {
    const auto& identity = bridge_report::runtime::service_identity();
    EXPECT_EQ(identity["build_id"].asString().size(), 64u);
    EXPECT_EQ(identity["source_digest"].asString().size(), 64u);
    EXPECT_FALSE(identity["configuration"].asString().empty());
    EXPECT_FALSE(identity["instance_id"].asString().empty());
    EXPECT_TRUE(identity["worktree_dirty"].isBool());
}
TEST(BuildInfoTest, ProcessIdentityIsStableAcrossHealthRequests) {
    const auto first = bridge_report::runtime::service_identity();
    EXPECT_EQ(first, bridge_report::runtime::service_identity());
}
