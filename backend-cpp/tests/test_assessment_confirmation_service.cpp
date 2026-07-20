#include <algorithm>

#include <gtest/gtest.h>

#include "bridge_report/assessment/AssessmentConfirmationService.hpp"
#include "support/h21_fixtures.hpp"

namespace assessment = bridge_report::assessment;
namespace standards = bridge_report::standards;

namespace {

assessment::AssessmentContextSnapshot confirmed_context(
    const standards::StandardPackage& package) {
    assessment::AssessmentContextSnapshot context;
    context.standard_package_id = "11111111-1111-1111-1111-111111111111";
    context.standard_profile_id = "22222222-2222-2222-2222-222222222222";
    context.inventory_revision_id = "33333333-3333-3333-3333-333333333333";
    context.inventory_confirmed = true;
    const auto input = bridge_report::tests::h21::complete_beam_input(package);
    context.bridge_type_id = input.bridge_type_id;
    for (const auto& component : input.components) {
        context.components.push_back(
            {component.component_instance_id, component.component_type_id});
    }
    return context;
}

Json::Value draft_with_forged_word_rating(
    const assessment::AssessmentContextSnapshot& context,
    double forged_score) {
    Json::Value draft;
    draft["defects"] = Json::Value(Json::arrayValue);
    const auto bearing = std::find_if(
        context.components.begin(), context.components.end(),
        [](const auto& item) {
            return item.component_type_id == "h21.component.bearing";
        });
    EXPECT_NE(bearing, context.components.end());
    Json::Value defect;
    defect["candidate_id"] = "defect-confirm-1";
    defect["bridge_component_id"] = bearing->component_instance_id;
    defect["standard_component_category_id"] = bearing->component_type_id;
    defect["defect_type"] = "板式支座老化变质、开裂";
    defect["defect_scale"] = 2;
    defect["review_status"] = "已确认";
    draft["defects"].append(defect);
    draft["ratings"]["overall"]["total_score"] = forged_score;
    draft["ratings"]["overall"]["overall_grade"] = "伪造等级";
    return draft;
}

}  // namespace

TEST(AssessmentConfirmationServiceTest, FormalCalculationIgnoresImportedRatings) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = confirmed_context(package);

    const auto first = assessment::calculate_assessment_confirmation(
        evaluator, package, context, draft_with_forged_word_rating(context, 1.0));
    const auto second = assessment::calculate_assessment_confirmation(
        evaluator, package, context, draft_with_forged_word_rating(context, 99.9));

    ASSERT_TRUE(first.preview.result.has_value());
    ASSERT_TRUE(second.preview.result.has_value());
    EXPECT_TRUE(first.issues.empty());
    EXPECT_TRUE(second.issues.empty());
    EXPECT_EQ(first.preview.input_checksum, second.preview.input_checksum);
    EXPECT_DOUBLE_EQ(
        first.preview.result->overall_score,
        second.preview.result->overall_score);
}

TEST(AssessmentConfirmationServiceTest, FormalCalculationMatchesLatestPreviewForSameInput) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = confirmed_context(package);
    const auto draft = draft_with_forged_word_rating(context, 12.3);

    const auto preview = assessment::calculate_assessment_preview(
        evaluator, package, context, draft, 42);
    const auto formal = assessment::calculate_assessment_confirmation(
        evaluator, package, context, draft);

    ASSERT_TRUE(preview.result.has_value());
    ASSERT_TRUE(formal.preview.result.has_value());
    EXPECT_EQ(preview.input_checksum, formal.preview.input_checksum);
    EXPECT_EQ(
        assessment::assessment_result_to_json(*preview.result),
        assessment::assessment_result_to_json(*formal.preview.result));
}

TEST(AssessmentConfirmationServiceTest, MissingScaleIsAStableFormalBlocker) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = confirmed_context(package);
    auto draft = draft_with_forged_word_rating(context, 100.0);
    draft["defects"][0]["defect_scale"] = Json::Value(Json::nullValue);

    const auto formal = assessment::calculate_assessment_confirmation(
        evaluator, package, context, draft);

    ASSERT_FALSE(formal.preview.result.has_value());
    ASSERT_EQ(formal.issues.size(), 1u);
    EXPECT_EQ(formal.issues.front().code, "assessment_defect_scale_required");
    EXPECT_EQ(formal.issues.front().entity_id, "defect-confirm-1");
    EXPECT_EQ(formal.issues.front().field_path, "defect_scale");
}
