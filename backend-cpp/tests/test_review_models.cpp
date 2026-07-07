#include <optional>
#include <string>

#include <gtest/gtest.h>
#include <json/value.h>

#include "bridge_report/review/ReviewModels.hpp"

using bridge_report::review::BridgeSummary;
using bridge_report::review::ImportRecordSummary;
using bridge_report::review::InspectionYearSummary;

TEST(BridgeSummaryTest, to_json_outputs_all_fields) {
    BridgeSummary summary{};
    summary.id = "b1111111-1111-1111-1111-111111111111";
    summary.system_number = "QL-000001";
    summary.bridge_name = "M05T2测试桥梁";
    summary.route_name = "G1线";
    summary.status = "在用";

    const auto json = summary.to_json();

    EXPECT_EQ(json["id"].asString(), "b1111111-1111-1111-1111-111111111111");
    EXPECT_EQ(json["system_number"].asString(), "QL-000001");
    EXPECT_EQ(json["bridge_name"].asString(), "M05T2测试桥梁");
    EXPECT_EQ(json["route_name"].asString(), "G1线");
    EXPECT_EQ(json["status"].asString(), "在用");
}

TEST(BridgeSummaryTest, to_json_outputs_null_for_missing_route_name) {
    BridgeSummary summary{};
    summary.id = "b1111111-1111-1111-1111-111111111111";
    summary.system_number = "QL-000001";
    summary.bridge_name = "M05T2测试桥梁";
    summary.route_name = std::nullopt;
    summary.status = "停用";

    const auto json = summary.to_json();

    EXPECT_TRUE(json["route_name"].isNull());
    EXPECT_EQ(json["status"].asString(), "停用");
}

TEST(InspectionYearSummaryTest, to_json_outputs_all_fields) {
    InspectionYearSummary summary{};
    summary.id = "y1111111-1111-1111-1111-111111111111";
    summary.system_number = "NDJC-000001";
    summary.inspection_year = 2025;
    summary.status = "已确认";
    summary.version_number = 2;
    summary.is_current = true;

    const auto json = summary.to_json();

    EXPECT_EQ(json["id"].asString(), "y1111111-1111-1111-1111-111111111111");
    EXPECT_EQ(json["system_number"].asString(), "NDJC-000001");
    EXPECT_EQ(json["inspection_year"].asInt(), 2025);
    EXPECT_EQ(json["status"].asString(), "已确认");
    EXPECT_EQ(json["version_number"].asInt(), 2);
    EXPECT_TRUE(json["is_current"].asBool());
}

TEST(InspectionYearSummaryTest, to_json_reflects_is_current_false) {
    InspectionYearSummary summary{};
    summary.id = "y1111111-1111-1111-1111-111111111111";
    summary.system_number = "NDJC-000002";
    summary.inspection_year = 2024;
    summary.status = "已被修订";
    summary.version_number = 1;
    summary.is_current = false;

    const auto json = summary.to_json();

    EXPECT_FALSE(json["is_current"].asBool());
    EXPECT_EQ(json["status"].asString(), "已被修订");
}

TEST(ImportRecordSummaryTest, to_json_outputs_all_fields_when_present) {
    ImportRecordSummary summary{};
    summary.id = "i1111111-1111-1111-1111-111111111111";
    summary.system_number = "DRJL-000001";
    summary.import_name = "2025年度报告.docx";
    summary.source_type = "正式Word";
    summary.import_status = "待校对";
    summary.inspection_year_id = "y1111111-1111-1111-1111-111111111111";
    summary.importer_name = "张三";
    summary.created_at = "2025-01-01 10:00:00+08";

    const auto json = summary.to_json();

    EXPECT_EQ(json["id"].asString(), "i1111111-1111-1111-1111-111111111111");
    EXPECT_EQ(json["system_number"].asString(), "DRJL-000001");
    EXPECT_EQ(json["import_name"].asString(), "2025年度报告.docx");
    EXPECT_EQ(json["source_type"].asString(), "正式Word");
    EXPECT_EQ(json["import_status"].asString(), "待校对");
    EXPECT_EQ(json["inspection_year_id"].asString(), "y1111111-1111-1111-1111-111111111111");
    EXPECT_EQ(json["importer_name"].asString(), "张三");
    EXPECT_EQ(json["created_at"].asString(), "2025-01-01 10:00:00+08");
}

TEST(ImportRecordSummaryTest, to_json_outputs_null_for_missing_optional_fields) {
    ImportRecordSummary summary{};
    summary.id = "i1111111-1111-1111-1111-111111111111";
    summary.system_number = "DRJL-000002";
    summary.import_name = "病害表.xlsx";
    summary.source_type = "Excel病害表";
    summary.import_status = "已上传";
    summary.inspection_year_id = std::nullopt;
    summary.importer_name = std::nullopt;
    summary.created_at = "2025-02-01 09:00:00+08";

    const auto json = summary.to_json();

    EXPECT_TRUE(json["inspection_year_id"].isNull());
    EXPECT_TRUE(json["importer_name"].isNull());
    EXPECT_EQ(json["import_status"].asString(), "已上传");
}
