#include <string>

#include <gtest/gtest.h>

#include "bridge_report/http/WorkspaceRoutes.hpp"

using bridge_report::http::WorkspaceResource;
using bridge_report::http::workspace_not_found_body;
using bridge_report::http::inspection_year_already_exists_body;
using bridge_report::http::is_supported_word_source_type;

TEST(WorkspaceRoutesTest, BuildsStableBridgeNotFoundError) {
    const auto body = workspace_not_found_body(WorkspaceResource::Bridge);
    EXPECT_EQ(body["code"].asString(), "bridge_not_found");
    EXPECT_EQ(body["message"].asString(), "桥梁不存在。");
}

TEST(WorkspaceRoutesTest, BuildsStableInspectionYearNotFoundError) {
    const auto body = workspace_not_found_body(WorkspaceResource::InspectionYear);
    EXPECT_EQ(body["code"].asString(), "inspection_year_not_found");
    EXPECT_EQ(body["message"].asString(), "年度检测不存在。");
}

TEST(WorkspaceRoutesTest, BuildsDuplicateInspectionErrorWithExistingId) {
    const auto body = inspection_year_already_exists_body(
        2026, "y1111111-1111-1111-1111-111111111111");
    EXPECT_EQ(body["code"].asString(), "inspection_year_already_exists");
    EXPECT_NE(body["message"].asString().find("2026"), std::string::npos);
    EXPECT_EQ(body["existing_inspection_year_id"].asString(),
              "y1111111-1111-1111-1111-111111111111");
}

TEST(WorkspaceRoutesTest, UploadWordOnlyAcceptsCurrentWordSourceTypes) {
    EXPECT_TRUE(is_supported_word_source_type("软件导出Word"));
    EXPECT_TRUE(is_supported_word_source_type("正式Word"));
    EXPECT_FALSE(is_supported_word_source_type("Excel病害表"));
    EXPECT_FALSE(is_supported_word_source_type(""));
}

TEST(WorkspaceRoutesTest, CreateInspectionRequiresBothStandardPackageIds) {
    bridge_report::http::CreateInspectionRequest parsed;
    Json::Value valid;
    valid["inspection_year"] = 2026;
    valid["technical_condition_package_id"] = "11111111-1111-1111-1111-111111111111";
    valid["maintenance_package_id"] = "22222222-2222-2222-2222-222222222222";
    EXPECT_FALSE(bridge_report::http::parse_create_inspection_request(valid, parsed).has_value());

    valid.removeMember("maintenance_package_id");
    ASSERT_TRUE(bridge_report::http::parse_create_inspection_request(valid, parsed).has_value());
    EXPECT_EQ(*bridge_report::http::parse_create_inspection_request(valid, parsed),
              "standard_packages_required");
}
