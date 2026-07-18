#include <gtest/gtest.h>

#include "support/h21_fixtures.hpp"

namespace {

using bridge_report::standards::ComponentCategoryAssessmentResult;
using bridge_report::standards::StructurePart;
using bridge_report::standards::StructurePartAssessmentResult;
using bridge_report::tests::h21::component;

const StructurePartAssessmentResult& part(
    const bridge_report::standards::BridgeAssessmentResult& result,
    StructurePart expected) {
    for (const auto& item : result.structure_parts) {
        if (item.structure_part == expected) {
            return item;
        }
    }
    throw std::runtime_error("structure part not found");
}

const ComponentCategoryAssessmentResult& category(
    const bridge_report::standards::BridgeAssessmentResult& result,
    const std::string& type_id) {
    for (const auto& item : result.structure_parts) {
        for (const auto& candidate : item.categories) {
            if (candidate.component_type_id == type_id) {
                return candidate;
            }
        }
    }
    throw std::runtime_error("category not found");
}

TEST(H21PartEvaluationTest, CategoryUsesMeanMinimumAndComponentCountFactor) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    auto& first = component(input, "h21.component.beam.upper_bearing");
    first.defects = {{"h21.defect.5_1_1_1", 3}};
    input.components.push_back({
        "h21.component.beam.upper_bearing.instance.2",
        "h21.component.beam.upper_bearing",
        {},
    });
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    const auto& result = category(*outcome.result, "h21.component.beam.upper_bearing");
    EXPECT_DOUBLE_EQ(result.mean_component_score, 82.5);
    EXPECT_DOUBLE_EQ(result.minimum_component_score, 65.0);
    ASSERT_TRUE(result.component_count_factor.has_value());
    EXPECT_DOUBLE_EQ(*result.component_count_factor, 10.0);
    EXPECT_DOUBLE_EQ(result.score, 79.0);
    EXPECT_FALSE(result.low_score_passthrough);
}

TEST(H21PartEvaluationTest, MajorCategoryBelow40PassesThroughMinimumScore) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.beam.upper_bearing").defects = {
        {"h21.defect.5_1_1_5", 5},
    };
    input.components.push_back({
        "h21.component.beam.upper_bearing.instance.2",
        "h21.component.beam.upper_bearing",
        {},
    });
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    const auto& result = category(*outcome.result, "h21.component.beam.upper_bearing");
    EXPECT_TRUE(result.major);
    EXPECT_TRUE(result.low_score_passthrough);
    EXPECT_DOUBLE_EQ(result.score, 0.0);
}

TEST(H21PartEvaluationTest, InterpolatesCountFactorForUnlistedComponentCount) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.beam.upper_bearing").defects = {
        {"h21.defect.5_1_1_1", 3},
    };
    for (int index = 2; index <= 35; ++index) {
        input.components.push_back({
            "h21.component.beam.upper_bearing.instance." + std::to_string(index),
            "h21.component.beam.upper_bearing",
            {},
        });
    }
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    const auto& result = category(*outcome.result, "h21.component.beam.upper_bearing");
    ASSERT_TRUE(result.component_count_factor.has_value());
    EXPECT_DOUBLE_EQ(*result.component_count_factor, 5.15);
    EXPECT_DOUBLE_EQ(result.mean_component_score, 99.0);
    EXPECT_NEAR(result.score, 99.0 - 35.0 / 5.15, 1e-12);
}

TEST(H21PartEvaluationTest, StructurePartUsesConfiguredCategoryWeights) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    auto& first = component(input, "h21.component.beam.upper_bearing");
    first.defects = {{"h21.defect.5_1_1_1", 3}};
    input.components.push_back({
        "h21.component.beam.upper_bearing.instance.2",
        "h21.component.beam.upper_bearing",
        {},
    });
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    const auto& super = part(*outcome.result, StructurePart::superstructure);
    EXPECT_NEAR(super.score, 79.0 * 0.70 + 100.0 * 0.18 + 100.0 * 0.12, 1e-12);
}

}  // namespace
