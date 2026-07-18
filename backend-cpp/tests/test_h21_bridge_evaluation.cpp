#include <array>
#include <set>

#include <gtest/gtest.h>

#include "support/h21_fixtures.hpp"

namespace {

using bridge_report::standards::StructurePart;
using bridge_report::tests::h21::component;

TEST(H21BridgeEvaluationTest, OverallUsesThreeStructurePartWeights) {
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
    const double superstructure = 79.0 * 0.70 + 100.0 * 0.18 + 100.0 * 0.12;
    EXPECT_NEAR(outcome.result->overall_score,
                superstructure * 0.40 + 100.0 * 0.40 + 100.0 * 0.20,
                1e-12);
}

TEST(H21BridgeEvaluationTest, EverySupportedBridgeTypeProducesCompleteHierarchy) {
    const auto package = bridge_report::tests::h21::load_package();
    bridge_report::standards::H21Evaluator evaluator(package);
    const std::array bridge_types = {
        "h21.bridge_type.beam",
        "h21.bridge_type.arch_slab_rib_box_double",
        "h21.bridge_type.arch_rigid_frame_truss",
        "h21.bridge_type.arch_steel_concrete_composite",
        "h21.bridge_type.suspension",
        "h21.bridge_type.cable_stayed",
    };

    for (const auto* bridge_type : bridge_types) {
        SCOPED_TRACE(bridge_type);
        const auto input = bridge_report::tests::h21::complete_input(package, bridge_type);
        const auto outcome = evaluator.evaluate(input);
        ASSERT_TRUE(outcome.ok());
        ASSERT_EQ(outcome.result->structure_parts.size(), 3u);
        EXPECT_DOUBLE_EQ(outcome.result->overall_score, 100.0);
        EXPECT_EQ(outcome.result->final_grade, 1);
    }
}

TEST(H21BridgeEvaluationTest, ProducesMachineReadableTraceForEveryLevel) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.beam.upper_bearing").defects = {
        {"h21.defect.5_1_1_1", 3},
    };
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    std::set<std::string> steps;
    for (const auto& entry : outcome.result->trace) {
        steps.insert(entry.step);
        EXPECT_FALSE(entry.rule_id.empty());
        EXPECT_FALSE(entry.source_reference.empty());
        EXPECT_TRUE(entry.inputs.isObject());
        EXPECT_TRUE(entry.output.isObject());
    }
    EXPECT_TRUE(steps.contains("defect_deduction"));
    EXPECT_TRUE(steps.contains("component_score"));
    EXPECT_TRUE(steps.contains("component_category_score"));
    EXPECT_TRUE(steps.contains("structure_part_score"));
    EXPECT_TRUE(steps.contains("overall_score"));
    EXPECT_TRUE(steps.contains("grade"));
    EXPECT_FALSE(outcome.result->explanation.empty());
}

}  // namespace
