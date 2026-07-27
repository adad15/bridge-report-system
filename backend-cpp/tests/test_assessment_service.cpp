#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/assessment/AssessmentService.hpp"
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
    EXPECT_LT(preview.result->overall_score, 100.0);
    EXPECT_FALSE(preview.result->trace.empty());
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

TEST(AssessmentServiceTest, UnknownIndicatorIsStructuredBlocker) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = complete_context(package);
    auto draft = draft_with_bearing_defect(context);
    draft["defects"][0]["standard_defect_indicator_id"] = "missing";

    const auto preview = assessment::calculate_assessment_preview(
        evaluator, package, context, draft, 4);
    ASSERT_FALSE(preview.issues.empty());
    EXPECT_EQ(preview.issues.front().code, "assessment_defect_indicator_unknown");
    EXPECT_FALSE(preview.result.has_value());
}
