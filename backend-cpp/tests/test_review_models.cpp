#include <optional>
#include <string>

#include <gtest/gtest.h>
#include <json/value.h>

#include "bridge_report/review/ReviewModels.hpp"
#include "bridge_report/review/ReviewStatistics.hpp"

using bridge_report::review::BridgeSummary;
using bridge_report::review::build_review_response;
using bridge_report::review::ImportRecordDetail;
using bridge_report::review::ImportRecordSummary;
using bridge_report::review::InspectionYearSummary;
using bridge_report::review::resolve_effective_inspection_year;
using bridge_report::review::ReviewStatistics;

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

namespace {

ImportRecordDetail make_detail_with_year() {
    ImportRecordDetail detail;
    detail.id = "i1111111-1111-1111-1111-111111111111";
    detail.system_number = "DRJL-000001";
    detail.bridge_id = "b1111111-1111-1111-1111-111111111111";
    detail.inspection_year_id = "y1111111-1111-1111-1111-111111111111";
    detail.import_name = "2025年度报告.docx";
    detail.source_type = "正式Word";
    detail.import_status = "待校对";
    detail.importer_name = "张三";
    detail.importer_version = "1.0.0";
    detail.parsed_result_json = "{}";
    detail.created_at = "2025-01-01 10:00:00+08";
    detail.updated_at = "2025-01-02 11:00:00+08";

    detail.bridge_system_number = "QL-000001";
    detail.bridge_name = "M05T2测试桥梁";
    detail.bridge_route_name = "G1线";

    detail.inspection_year_system_number = "NDJC-000001";
    detail.inspection_year = 2025;
    detail.inspection_year_status = "已确认";
    detail.inspection_year_version_number = 1;
    detail.inspection_year_is_current = true;

    return detail;
}

ImportRecordDetail make_detail_without_year() {
    ImportRecordDetail detail;
    detail.id = "i2222222-2222-2222-2222-222222222222";
    detail.system_number = "DRJL-000002";
    detail.bridge_id = "b1111111-1111-1111-1111-111111111111";
    detail.inspection_year_id = std::nullopt;
    detail.import_name = "病害表.xlsx";
    detail.source_type = "Excel病害表";
    detail.import_status = "已上传";
    detail.importer_name = std::nullopt;
    detail.importer_version = std::nullopt;
    detail.parsed_result_json = "{}";
    detail.created_at = "2025-02-01 09:00:00+08";
    detail.updated_at = "2025-02-01 09:00:00+08";

    detail.bridge_system_number = "QL-000001";
    detail.bridge_name = "M05T2测试桥梁";
    detail.bridge_route_name = std::nullopt;

    detail.inspection_year_system_number = std::nullopt;
    detail.inspection_year = std::nullopt;
    detail.inspection_year_status = std::nullopt;
    detail.inspection_year_version_number = std::nullopt;
    detail.inspection_year_is_current = std::nullopt;

    return detail;
}

}  // namespace

TEST(BuildReviewResponseTest, PopulatesInspectionYearObjectWhenPresent) {
    const auto detail = make_detail_with_year();
    Json::Value parsed_result(Json::objectValue);
    ReviewStatistics statistics{};

    const auto body = build_review_response(detail, parsed_result, statistics, /*has_current_annual_facts=*/true);

    ASSERT_TRUE(body["inspection_year"].isObject());
    EXPECT_EQ(body["inspection_year"]["id"].asString(), "y1111111-1111-1111-1111-111111111111");
    EXPECT_EQ(body["inspection_year"]["system_number"].asString(), "NDJC-000001");
    EXPECT_EQ(body["inspection_year"]["inspection_year"].asInt(), 2025);
    EXPECT_EQ(body["inspection_year"]["status"].asString(), "已确认");
    EXPECT_EQ(body["inspection_year"]["version_number"].asInt(), 1);
    EXPECT_TRUE(body["inspection_year"]["is_current"].asBool());
    EXPECT_TRUE(body["has_current_annual_facts"].asBool());

    EXPECT_EQ(body["import_record"]["id"].asString(), "i1111111-1111-1111-1111-111111111111");
    EXPECT_EQ(body["import_record"]["importer_name"].asString(), "张三");
    EXPECT_EQ(body["import_record"]["importer_version"].asString(), "1.0.0");
    EXPECT_EQ(body["bridge"]["id"].asString(), "b1111111-1111-1111-1111-111111111111");
    EXPECT_EQ(body["bridge"]["route_name"].asString(), "G1线");
}

TEST(BuildReviewResponseTest, OutputsNullInspectionYearWhenAbsent) {
    const auto detail = make_detail_without_year();
    Json::Value parsed_result(Json::objectValue);
    ReviewStatistics statistics{};

    const auto body = build_review_response(detail, parsed_result, statistics, /*has_current_annual_facts=*/false);

    EXPECT_TRUE(body["inspection_year"].isNull());
    EXPECT_FALSE(body["has_current_annual_facts"].asBool());
    EXPECT_TRUE(body["import_record"]["importer_name"].isNull());
    EXPECT_TRUE(body["import_record"]["importer_version"].isNull());
    EXPECT_TRUE(body["bridge"]["route_name"].isNull());
}

TEST(BuildReviewResponseTest, IncludesParsedResultAndStatisticsVerbatim) {
    const auto detail = make_detail_with_year();
    Json::Value parsed_result(Json::objectValue);
    parsed_result["defects"] = Json::Value(Json::arrayValue);
    parsed_result["defects"].append(Json::Value(Json::objectValue));
    ReviewStatistics statistics{};
    statistics.defect_count = 1;
    statistics.pending_count = 1;

    const auto body = build_review_response(detail, parsed_result, statistics, /*has_current_annual_facts=*/false);

    EXPECT_EQ(body["parsed_result"]["defects"].size(), 1u);
    EXPECT_EQ(body["statistics"]["defect_count"].asInt(), 1);
    EXPECT_EQ(body["statistics"]["pending_count"].asInt(), 1);
}

TEST(ResolveEffectiveInspectionYearTest, UsesAttachedYearWhenPresent) {
    const auto detail = make_detail_with_year();  // inspection_year = 2025
    Json::Value parsed_result(Json::objectValue);
    parsed_result["inspection"]["inspection_year"] = 2099;  // 挂载年度优先，不应被解析结果覆盖

    const auto year = resolve_effective_inspection_year(detail, parsed_result);

    ASSERT_TRUE(year.has_value());
    EXPECT_EQ(*year, 2025);
}

TEST(ResolveEffectiveInspectionYearTest, FallsBackToParsedInspectionYearWhenNoAttachedYear) {
    const auto detail = make_detail_without_year();
    Json::Value parsed_result(Json::objectValue);
    parsed_result["inspection"]["inspection_year"] = 2026;

    const auto year = resolve_effective_inspection_year(detail, parsed_result);

    ASSERT_TRUE(year.has_value());
    EXPECT_EQ(*year, 2026);
}

TEST(ResolveEffectiveInspectionYearTest, ReturnsNulloptWhenNeitherAttachedNorParsedYearPresent) {
    const auto detail = make_detail_without_year();
    Json::Value parsed_result(Json::objectValue);

    const auto year = resolve_effective_inspection_year(detail, parsed_result);

    EXPECT_FALSE(year.has_value());
}
