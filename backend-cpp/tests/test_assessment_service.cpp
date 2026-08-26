#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/assessment/AssessmentService.hpp"
#include "bridge_report/standards/DefectIndicatorResolver.hpp"
#include "support/h21_fixtures.hpp"

namespace assessment = bridge_report::assessment;
namespace standards = bridge_report::standards;

namespace {

assessment::AssessmentContextSnapshot complete_context(
    const standards::StandardPackage& package) {
    assessment::AssessmentContextSnapshot context;
    context.standard_package_id = "11111111-1111-1111-1111-111111111111";
    context.standard_profile_id = "22222222-2222-2222-2222-222222222222";
    context.inventory_revision_id = "33333333-3333-3333-3333-333333333333";
    context.inventory_confirmed = true;
    const auto input = bridge_report::tests::h21::complete_beam_input(package);
    context.bridge_type_id = input.bridge_type_id;
    for (const auto& component : input.components) {
        context.components.push_back({component.component_instance_id,
                                      component.component_type_id});
    }
    context.rating_tree_version_id = "test-tree-version";
    context.rating_tree_content_checksum =
        "sha256:test-rating-tree";
    context.rating_tree =
        bridge_report::tests::h21::single_indicator_tree(
            context.bridge_type_id,
            "h21.component.bearing",
            "h21.defect.5_3_1_1",
            {1, 2, 3});
    return context;
}

Json::Value draft_with_bearing_defect(
    const assessment::AssessmentContextSnapshot& context) {
    Json::Value draft;
    draft["defects"] = Json::Value(Json::arrayValue);
    const auto bearing = std::find_if(
        context.components.begin(), context.components.end(),
        [](const auto& item) { return item.component_type_id == "h21.component.bearing"; });
    EXPECT_NE(bearing, context.components.end());
    Json::Value defect;
    defect["candidate_id"] = "defect-1";
    defect["bridge_component_id"] = bearing->component_instance_id;
    defect["standard_component_category_id"] = bearing->component_type_id;
    defect["defect_type"] = "板式支座老化变质、开裂";
    defect["standard_defect_indicator_id"] = "h21.defect.5_3_1_1";
    defect["rating_tree_version_id"] = context.rating_tree_version_id;
    defect["rating_tree_node_id"] = "test-rating-tree-node";
    defect["defect_scale"] = 2;
    defect["review_status"] = "已修改";
    draft["defects"].append(defect);
    draft["ratings"]["overall"]["total_score"] = 1.0;
    return draft;
}

}  // namespace

TEST(AssessmentServiceTest, CompleteDraftReturnsH21ResultAndTrace) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = complete_context(package);
    const auto preview = assessment::calculate_assessment_preview(
        evaluator, package, context, draft_with_bearing_defect(context), 7);

    ASSERT_TRUE(preview.result.has_value());
    EXPECT_TRUE(preview.issues.empty());
    EXPECT_EQ(preview.client_revision, 7);
    EXPECT_EQ(preview.standard_identity["standard_code"].asString(), "JTG/T H21—2011");
    EXPECT_EQ(
        preview.standard_identity["rating_tree"]["version_id"].asString(),
        "test-tree-version");
    EXPECT_LT(preview.result->overall_score, 100.0);
    EXPECT_FALSE(preview.result->trace.empty());
}

TEST(AssessmentServiceTest, CrossVersionTreeNodeIsAStableBlocker) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = complete_context(package);
    auto draft = draft_with_bearing_defect(context);
    draft["defects"][0]["rating_tree_version_id"] = "other-tree";

    const auto preview = assessment::calculate_assessment_preview(
        evaluator, package, context, draft, 8);
    ASSERT_FALSE(preview.issues.empty());
    EXPECT_EQ(
        preview.issues.front().code,
        "assessment_rating_tree_node_required");
}

TEST(AssessmentServiceTest, NonScoringTreeNodeIsSkippedWithTrace) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    auto context = complete_context(package);
    auto draft = draft_with_bearing_defect(context);
    auto& node = context.rating_tree->nodes.at("test-rating-tree-node");
    node.display_name = "其他病害（暂不计分）";
    node.scoring_mode =
        bridge_report::rating_tree::RatingTreeScoringMode::non_scoring;
    node.h21_indicator_id.reset();
    node.is_scoring = false;
    node.allowed_scales.clear();
    draft["defects"][0]["standard_defect_indicator_id"] =
        Json::Value();
    draft["defects"][0]["defect_scale"] = Json::Value();

    const auto preview = assessment::calculate_assessment_preview(
        evaluator, package, context, draft, 9);
    ASSERT_TRUE(preview.result.has_value());
    EXPECT_TRUE(preview.issues.empty());
    ASSERT_EQ(
        preview.input_summary["rating_tree_skips"].size(), 1u);
    EXPECT_EQ(
        preview.input_summary["rating_tree_skips"][0]["reason"].asString(),
        "rating_tree_non_scoring");
}

TEST(AssessmentServiceTest, WaterDamageCanReferenceH21Carbonization) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    auto context = complete_context(package);
    const std::string indicator_id = "h21.defect.5_1_1_6";
    const auto component = std::find_if(
        context.components.begin(),
        context.components.end(),
        [&](const auto& item) {
            return standards::resolve_defect_indicator(
                package, indicator_id, item.component_type_id, 2).ok();
        });
    ASSERT_NE(component, context.components.end());
    context.rating_tree =
        bridge_report::tests::h21::single_indicator_tree(
            context.bridge_type_id,
            component->component_type_id,
            indicator_id,
            {1, 2, 3, 4});
    context.rating_tree->nodes.at("test-rating-tree-node").display_name =
        "水损（参照混凝土碳化）";
    context.rating_tree->nodes.at("test-rating-tree-node").scoring_mode =
        bridge_report::rating_tree::RatingTreeScoringMode::reference_h21;

    Json::Value draft;
    draft["defects"] = Json::Value(Json::arrayValue);
    Json::Value defect;
    defect["candidate_id"] = "water-damage";
    defect["bridge_component_id"] = component->component_instance_id;
    defect["standard_component_category_id"] =
        component->component_type_id;
    defect["rating_tree_version_id"] =
        context.rating_tree_version_id;
    defect["rating_tree_node_id"] = "test-rating-tree-node";
    defect["standard_defect_indicator_id"] = indicator_id;
    defect["defect_scale"] = 2;
    defect["review_status"] = "已确认";
    draft["defects"].append(defect);

    const auto preview = assessment::calculate_assessment_preview(
        evaluator, package, context, draft, 10);
    ASSERT_TRUE(preview.result.has_value());
    EXPECT_TRUE(preview.issues.empty());
    EXPECT_EQ(
        preview.input_summary["defects"][0]["defect_indicator_id"].asString(),
        indicator_id);
}

TEST(AssessmentServiceTest, SameComponentAndH21IndicatorUsesHighestScaleOnce) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    auto context = complete_context(package);
    auto highest_only = draft_with_bearing_defect(context);
    highest_only["defects"][0]["defect_scale"] = 3;

    auto duplicate = highest_only;
    auto alias_node =
        context.rating_tree->nodes.at("test-rating-tree-node");
    alias_node.id = "test-rating-tree-node-alias";
    alias_node.display_name = "单位同义病害";
    context.rating_tree->nodes.emplace(alias_node.id, alias_node);
    auto lower = duplicate["defects"][0];
    lower["candidate_id"] = "defect-lower-scale";
    lower["rating_tree_node_id"] = alias_node.id;
    lower["defect_scale"] = 1;
    duplicate["defects"].append(lower);

    const auto first = assessment::calculate_assessment_preview(
        evaluator, package, context, highest_only, 11);
    const auto second = assessment::calculate_assessment_preview(
        evaluator, package, context, duplicate, 12);
    ASSERT_TRUE(first.result.has_value());
    ASSERT_TRUE(second.result.has_value());
    EXPECT_DOUBLE_EQ(
        first.result->overall_score,
        second.result->overall_score);
}

TEST(AssessmentServiceTest, ImportedRatingFieldsCannotOverrideServerCalculation) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = complete_context(package);
    auto first_draft = draft_with_bearing_defect(context);
    auto second_draft = first_draft;
    second_draft["ratings"]["overall"]["total_score"] = 99.9;

    const auto first = assessment::calculate_assessment_preview(evaluator, package, context, first_draft, 1);
    const auto second = assessment::calculate_assessment_preview(evaluator, package, context, second_draft, 2);

    ASSERT_TRUE(first.result.has_value());
    ASSERT_TRUE(second.result.has_value());
    EXPECT_DOUBLE_EQ(first.result->overall_score, second.result->overall_score);
    EXPECT_EQ(first.input_checksum, second.input_checksum);
}

TEST(AssessmentServiceTest, MissingScaleAndUnconfirmedInventoryAreStructuredBlockers) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    auto context = complete_context(package);
    auto draft = draft_with_bearing_defect(context);
    draft["defects"][0]["defect_scale"] = Json::Value(Json::nullValue);

    auto preview = assessment::calculate_assessment_preview(evaluator, package, context, draft, 1);
    ASSERT_FALSE(preview.issues.empty());
    EXPECT_EQ(preview.issues.front().code, "assessment_defect_scale_required");
    EXPECT_EQ(preview.issues.front().entity_id, "defect-1");
    EXPECT_EQ(preview.issues.front().field_path, "defect_scale");

    context.inventory_confirmed = false;
    preview = assessment::calculate_assessment_preview(evaluator, package, context, draft, 2);
    ASSERT_FALSE(preview.issues.empty());
    EXPECT_EQ(preview.issues.front().code, "assessment_inventory_not_confirmed");
}

TEST(AssessmentServiceTest, StableIndicatorIdRemainsTruthWhenDisplayNameChanges) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = complete_context(package);
    auto draft = draft_with_bearing_defect(context);
    draft["defects"][0]["defect_type"] = "不存在的病害类型";

    const auto preview = assessment::calculate_assessment_preview(evaluator, package, context, draft, 3);
    EXPECT_TRUE(preview.issues.empty());
    EXPECT_TRUE(preview.result.has_value());
    EXPECT_EQ(
        preview.input_summary["defects"][0]["defect_indicator_name"].asString(),
        "板式支座老化变质、开裂");
}

TEST(AssessmentServiceTest, MissingDerivedIndicatorUsesRatingTreeNode) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = complete_context(package);
    auto draft = draft_with_bearing_defect(context);
    draft["defects"][0]["standard_defect_indicator_id"] = Json::Value();
    draft["defects"][0]["rating_tree_match_method"] = "manual";

    const auto preview = assessment::calculate_assessment_preview(
        evaluator, package, context, draft, 4);

    EXPECT_TRUE(preview.issues.empty());
    ASSERT_TRUE(preview.result.has_value());
    EXPECT_EQ(
        preview.input_summary["defects"][0]["defect_indicator_id"].asString(),
        "h21.defect.5_3_1_1");
}

TEST(AssessmentServiceTest, UnknownIndicatorIsStructuredBlocker) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = complete_context(package);
    auto draft = draft_with_bearing_defect(context);
    draft["defects"][0]["standard_defect_indicator_id"] = "missing";

    const auto preview = assessment::calculate_assessment_preview(
        evaluator, package, context, draft, 4);
    ASSERT_FALSE(preview.issues.empty());
    EXPECT_EQ(
        preview.issues.front().code,
        "assessment_rating_tree_indicator_mismatch");
    EXPECT_FALSE(preview.result.has_value());
}

// ── 已入库评定的只读还原 ──────────────────────────────────────────────
// 入库时把整份结果原样存进了 result_summary_json，读回来不许重算：规则包若已升级，
// 重算出来的数字会和当年写进报告的对不上。

TEST(ConfirmedAssessmentReportTest, RestoresStoredResultAndStandardWithoutRecomputing) {
    Json::Value result_summary;
    result_summary["result"]["overall_score"] = 83.25;
    result_summary["result"]["final_grade"] = 2;
    result_summary["result"]["structure_parts"] = Json::Value(Json::arrayValue);
    result_summary["issues"] = Json::Value(Json::arrayValue);
    Json::Value rule_package;
    rule_package["standard_code"] = "JTG/T H21-2011";
    rule_package["package_version"] = "1.0.3";

    const auto report =
        assessment::build_confirmed_assessment_report(result_summary, rule_package);

    EXPECT_DOUBLE_EQ(report.result["overall_score"].asDouble(), 83.25);
    EXPECT_EQ(report.standard_identity["package_version"].asString(), "1.0.3");
    EXPECT_TRUE(report.issues.isArray());
    EXPECT_EQ(report.issues.size(), 0U);
}

// 试算与只读回执共用同一套前端渲染，靠的就是这三个键同名同形。
TEST(ConfirmedAssessmentReportTest, EnvelopeKeepsTheSameShapeAsThePreview) {
    Json::Value result_summary;
    result_summary["result"]["overall_score"] = 90.0;
    result_summary["issues"] = Json::Value(Json::arrayValue);
    Json::Value rule_package;
    rule_package["standard_name"] = "公路桥梁技术状况评定标准";

    auto report = assessment::build_confirmed_assessment_report(result_summary, rule_package);
    report.assessment_run_id = "44444444-4444-4444-4444-444444444444";
    report.formal_revision_number = 2;
    report.is_current = false;
    report.inspection_year = 2024;
    report.inspection_year_version = 1;
    const auto json = report.to_json();

    EXPECT_TRUE(json.isMember("standard"));
    EXPECT_TRUE(json.isMember("result"));
    EXPECT_TRUE(json.isMember("issues"));
    EXPECT_DOUBLE_EQ(json["result"]["overall_score"].asDouble(), 90.0);
    EXPECT_EQ(json["formal_revision_number"].asInt(), 2);
    EXPECT_FALSE(json["is_current"].asBool());
    EXPECT_EQ(json["inspection_year"].asInt(), 2024);
    // 没确认时间就给 null，不要拿空字符串冒充一个时间戳。
    EXPECT_TRUE(json["confirmed_at"].isNull());
}

// 运行行还停在 '{}' 的情况：宁可报"没有可读的结果"，也不要编一个 0 分出来。
TEST(ConfirmedAssessmentReportTest, LeavesTheResultNullWhenTheRunStoredNothing) {
    const auto report = assessment::build_confirmed_assessment_report(
        Json::Value(Json::objectValue), Json::Value(Json::objectValue));

    EXPECT_TRUE(report.result.isNull());
    EXPECT_TRUE(report.to_json()["result"].isNull());
}
