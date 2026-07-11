#include <thread>

#include <gtest/gtest.h>

#include "bridge_report/db/CommitLatch.hpp"

TEST(CommitLatchTest, ReturnsFalseForFailedCommitCallback) {
    bridge_report::db::CommitLatch latch;
    latch.callback()(false);
    EXPECT_FALSE(latch.wait());
}

TEST(CommitLatchTest, WaitsUntilSuccessfulCommitCallbackArrives) {
    bridge_report::db::CommitLatch latch;
    auto callback = latch.callback();
    std::thread worker([callback] { callback(true); });
    EXPECT_TRUE(latch.wait());
    worker.join();
}

TEST(CommitLatchTest, ReturnsFalseWhenCommitCallbackNeverArrives) {
    bridge_report::db::CommitLatch latch;
    EXPECT_FALSE(latch.wait(std::chrono::milliseconds(1)));
}
