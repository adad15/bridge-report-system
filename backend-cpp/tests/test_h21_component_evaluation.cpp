#include <algorithm>
#include <cmath>

#include <gtest/gtest.h>

#include "support/h21_fixtures.hpp"

namespace {

using bridge_report::standards::ComponentAssessmentResult;
using bridge_report::tests::h21::component;

const ComponentAssessmentResult& evaluated_component(
    const bridge_report::standards::BridgeAssessmentResult& result,
    const std::string& type_id) {
    for (const auto& part : result.structure_parts) {
        for (const auto& category : part.categories) {
            if (category.component_type_id == type_id) {
                return category.components.front();
            }
        }
    }
    throw std::runtime_error("evaluated component not found");
}

TEST(H21ComponentEvaluationTest, CompleteInventoryWithoutDefectsScoresEveryComponentAt100) {
    const auto package = bridge_report::tests::h21::load_package();
    const auto input = bridge_report::tests::h21::complete_beam_input(package);
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    for (const auto& part : outcome.result->structure_parts) {
        for (const auto& category : part.categories) {
            for (const auto& item : category.components) {
                EXPECT_DOUBLE_EQ(item.score, 100.0);
            }
        }
    }
    EXPECT_DOUBLE_EQ(outcome.result->overall_score, 100.0);
}

TEST(H21ComponentEvaluationTest, ConvertsScaleToDeductionAndScoresOneDefect) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.beam.upper_bearing").defects = {
        {"h21.defect.5_1_1_1", 3},
    };
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    const auto& result = evaluated_component(
        *outcome.result, "h21.component.beam.upper_bearing");
    ASSERT_EQ(result.defects.size(), 1u);
    EXPECT_DOUBLE_EQ(result.defects.front().deduction, 35.0);
    EXPECT_DOUBLE_EQ(result.score, 65.0);
}

TEST(H21ComponentEvaluationTest, MultipleDefectsAreOrderIndependentWithoutEarlyRounding) {
    const auto package = bridge_report::tests::h21::load_package();
    bridge_report::standards::H21Evaluator evaluator(package);
    auto first = bridge_report::tests::h21::complete_beam_input(package);
    component(first, "h21.component.beam.upper_bearing").defects = {
        {"h21.defect.5_1_1_1", 3},
        {"h21.defect.5_1_1_2", 2},
    };
    auto second = first;
    std::reverse(
        component(second, "h21.component.beam.upper_bearing").defects.begin(),
        component(second, "h21.component.beam.upper_bearing").defects.end());

    const auto first_outcome = evaluator.evaluate(first);
    const auto second_outcome = evaluator.evaluate(second);

    ASSERT_TRUE(first_outcome.ok());
    ASSERT_TRUE(second_outcome.ok());
    const auto first_score = evaluated_component(
        *first_outcome.result, "h21.component.beam.upper_bearing").score;
    const auto second_score = evaluated_component(
        *second_outcome.result, "h21.component.beam.upper_bearing").score;
    const double manual = 100.0 - 35.0 - 25.0 / (100.0 * std::sqrt(2.0)) * 65.0;
    EXPECT_DOUBLE_EQ(first_score, manual);
    EXPECT_DOUBLE_EQ(second_score, manual);
}

TEST(H21ComponentEvaluationTest, Deduction100ForcesComponentScoreToZero) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.beam.upper_bearing").defects = {
        {"h21.defect.5_1_1_5", 5},
        {"h21.defect.5_1_1_1", 2},
    };
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    EXPECT_DOUBLE_EQ(evaluated_component(
        *outcome.result, "h21.component.beam.upper_bearing").score, 0.0);
}

}  // namespace
