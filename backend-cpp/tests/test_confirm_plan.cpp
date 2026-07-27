#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/ConfirmPlan.hpp"

namespace {

Json::Value fixture() {
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
        "samples/contracts/bridge_annual_inspection_data.v3.valid.json";
    std::ifstream input(path, std::ios::binary);
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!input || !Json::parseFromStream(builder, input, &root, &errors)) {
        throw std::runtime_error("unable to load v2 fixture: " + errors);
    }
    return root;
}

void settle(Json::Value& data) {
    auto& defect = data["defects"][0];
    defect["bridge_component_id"] = "00000000-0000-0000-0000-000000000101";
    defect["standard_component_category_id"] = "category-main-girder";
    defect["resolved_structure_part"] = "上部结构";
    defect["component_inventory_revision_id"] = "00000000-0000-0000-0000-000000000201";
    defect["review_status"] = "已确认";
    defect["group_review_status"] = "已确认";
    data["photos"][0]["match_status"] = "已确认";
    data["photos"][0]["review_status"] = "已确认";
}

}  // namespace

TEST(ConfirmPlanTest, SkipsPendingCandidates) {
    const auto plan = bridge_report::review::build_confirm_plan(fixture());
    EXPECT_TRUE(plan.components.empty());
    EXPECT_TRUE(plan.defects.empty());
    EXPECT_TRUE(plan.photos.empty());
}

TEST(ConfirmPlanTest, MapsSettledVersionTwoFactsWithoutImportedRatings) {
    auto data = fixture();
    settle(data);
    const auto plan = bridge_report::review::build_confirm_plan(data);

    ASSERT_EQ(plan.components.size(), 1u);
    EXPECT_EQ(
        plan.components[0].existing_bridge_component_id,
        "00000000-0000-0000-0000-000000000101");
    EXPECT_EQ(plan.components[0].business_component_code, "2-1#梁");

    ASSERT_EQ(plan.defects.size(), 1u);
    EXPECT_EQ(plan.defects[0].candidate_id, "defect_0001");
    EXPECT_EQ(plan.defects[0].defect_location, "第二跨左幅梁底");
    EXPECT_EQ(plan.defects[0].scale, "2");
    EXPECT_EQ(plan.defects[0].measurements.size(), 3u);

    ASSERT_EQ(plan.photos.size(), 1u);
    EXPECT_EQ(plan.photos[0].defect_candidate_id, "defect_0001");
    EXPECT_EQ(plan.photos[0].archive_relative_path, "photos/2.1-1.jpg");
}

TEST(ConfirmPlanTest, SkipsASettledDefectWithoutActualComponentAssociation) {
    auto data = fixture();
    data["defects"][0]["review_status"] = "已确认";
    data["defects"][0]["group_review_status"] = "已确认";
    const auto plan = bridge_report::review::build_confirm_plan(data);
    EXPECT_TRUE(plan.components.empty());
    EXPECT_TRUE(plan.defects.empty());
}

TEST(ConfirmPlanTest, KeepsRangeMeasurementBounds) {
    auto data = fixture();
    settle(data);
    auto& measurement = data["defects"][0]["measurements"][0];
    measurement["value_type"] = "range";
    measurement["value"] = Json::Value(Json::nullValue);
    measurement["minimum_value"] = 0.5;
    measurement["maximum_value"] = 4.0;
    measurement["source_text"] = "0.5~4.0m";

    const auto plan = bridge_report::review::build_confirm_plan(data);
    ASSERT_FALSE(plan.defects.empty());
    const auto& mapped = plan.defects[0].measurements[0];
    EXPECT_EQ(mapped.value_type, "range");
    EXPECT_DOUBLE_EQ(*mapped.minimum_value, 0.5);
    EXPECT_DOUBLE_EQ(*mapped.maximum_value, 4.0);
}

TEST(ConfirmPlanTest, IgnoresUnrelatedPhotos) {
    auto data = fixture();
    settle(data);
    data["photos"][0]["linked_defect_candidate_id"] = Json::Value(Json::nullValue);
    data["photos"][0]["match_status"] = "未关联";
    const auto plan = bridge_report::review::build_confirm_plan(data);
    EXPECT_TRUE(plan.photos.empty());
}

TEST(ConfirmPlanTest, ModifiedDefectAndPhotoRemainEligibleFacts) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["review_status"] = "已修改";
    data["photos"][0]["review_status"] = "已修改";
    const auto plan = bridge_report::review::build_confirm_plan(data);
    ASSERT_EQ(plan.defects.size(), 1u);
    ASSERT_EQ(plan.photos.size(), 1u);
    EXPECT_EQ(plan.defects[0].review_status, "已修改");
}

TEST(ConfirmPlanTest, IgnoredDefectNeverCreatesFormalFacts) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["review_status"] = "已忽略";
    const auto plan = bridge_report::review::build_confirm_plan(data);
    EXPECT_TRUE(plan.components.empty());
    EXPECT_TRUE(plan.defects.empty());
    EXPECT_TRUE(plan.photos.empty());
}

TEST(ConfirmPlanTest, TwoDefectsOnOneActualComponentShareOneComponentPlan) {
    auto data = fixture();
    settle(data);
    auto second = data["defects"][0];
    second["candidate_id"] = "defect_0002";
    second["defect_type"] = "渗水";
    second["photo_numbers"] = Json::Value(Json::arrayValue);
    data["defects"].append(second);
    const auto plan = bridge_report::review::build_confirm_plan(data);
    EXPECT_EQ(plan.components.size(), 1u);
    EXPECT_EQ(plan.defects.size(), 2u);
    EXPECT_EQ(plan.defects[0].component_key, plan.defects[1].component_key);
}

TEST(ConfirmPlanTest, DifferentActualComponentsCreateDistinctComponentPlans) {
    auto data = fixture();
    settle(data);
    auto second = data["defects"][0];
    second["candidate_id"] = "defect_0002";
    second["bridge_component_id"] = "00000000-0000-0000-0000-000000000102";
    second["component_number"] = "2-2#梁";
    second["photo_numbers"] = Json::Value(Json::arrayValue);
    data["defects"].append(second);
    const auto plan = bridge_report::review::build_confirm_plan(data);
    EXPECT_EQ(plan.components.size(), 2u);
    EXPECT_EQ(plan.defects.size(), 2u);
    EXPECT_NE(plan.defects[0].component_key, plan.defects[1].component_key);
}

TEST(ConfirmPlanTest, SeverityNeverBecomesRegulatoryScale) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["defect_scale"] = Json::Value(Json::nullValue);
    data["defects"][0]["severity"] = "warning";
    const auto plan = bridge_report::review::build_confirm_plan(data);
    ASSERT_EQ(plan.defects.size(), 1u);
    EXPECT_FALSE(plan.defects[0].scale.has_value());
}

TEST(ConfirmPlanTest, KeepsUnstructuredMeasurementTextAsAFormalRawRow) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["measurements"] = Json::Value(Json::arrayValue);
    data["defects"][0]["quantity_text"] = Json::Value(Json::nullValue);
    data["defects"][0]["measurement_text"] = "现场量测值待复核";
    const auto plan = bridge_report::review::build_confirm_plan(data);
    ASSERT_EQ(plan.defects.size(), 1u);
    ASSERT_EQ(plan.defects[0].measurements.size(), 1u);
    EXPECT_EQ(plan.defects[0].measurements[0].raw_text, "现场量测值待复核");
    EXPECT_FALSE(plan.defects[0].measurements[0].numeric_value.has_value());
}

TEST(ConfirmPlanTest, QuantityTextAddsOneQuantityMeasurement) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["quantity_text"] = "3处";
    const auto plan = bridge_report::review::build_confirm_plan(data);
    ASSERT_EQ(plan.defects.size(), 1u);
    ASSERT_EQ(plan.defects[0].measurements.size(), 3u);
    const auto& quantity = plan.defects[0].measurements.back();
    EXPECT_EQ(quantity.measurement_type, "数量");
    ASSERT_TRUE(quantity.numeric_value.has_value());
    EXPECT_DOUBLE_EQ(*quantity.numeric_value, 3.0);
}

TEST(ConfirmPlanTest, PendingOrIgnoredPhotosNeverEnterFormalPlan) {
    for (const auto* status : {"待确认", "已忽略"}) {
        auto data = fixture();
        settle(data);
        data["photos"][0]["review_status"] = status;
        const auto plan = bridge_report::review::build_confirm_plan(data);
        EXPECT_TRUE(plan.photos.empty()) << status;
    }
}
