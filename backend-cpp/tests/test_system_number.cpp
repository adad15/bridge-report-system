#include <stdexcept>

#include <gtest/gtest.h>

#include "bridge_report/identity/SystemNumber.hpp"

TEST(SystemNumberTest, FormatsSixDigitNumbers) {
    EXPECT_EQ(bridge_report::identity::format_system_number("QL", 1), "QL-000001");
    EXPECT_EQ(bridge_report::identity::format_system_number("NDJC", 42), "NDJC-000042");
    EXPECT_EQ(bridge_report::identity::format_system_number("BHDB", 123456), "BHDB-123456");
}

TEST(SystemNumberTest, RejectsInvalidSequenceValues) {
    EXPECT_THROW(bridge_report::identity::format_system_number("QL", 0), std::invalid_argument);
    EXPECT_THROW(bridge_report::identity::format_system_number("QL", -1), std::invalid_argument);
}

TEST(SystemNumberTest, KnowsModule02Prefixes) {
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("QL"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("QLBM"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("NDJC"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("GDWJ"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("DRJL"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("DRWJ"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("GJ"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("GJBM"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("BHGC"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("BHCC"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("BHZP"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("JSPD"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("BHXS"));
    EXPECT_TRUE(bridge_report::identity::is_supported_system_number_prefix("BHDB"));
    EXPECT_FALSE(bridge_report::identity::is_supported_system_number_prefix("REPORT"));
}
