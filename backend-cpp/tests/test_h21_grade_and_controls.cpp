#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

#include <gtest/gtest.h>

#include "support/h21_fixtures.hpp"

namespace {

using bridge_report::tests::h21::component;

Json::Value manual_cases() {
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
        "samples/scoring/standards/jtg-t-h21-2011/beam_manual_cases.json";
    std::ifstream input(path, std::ios::binary);
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!input || !Json::parseFromStream(builder, input, &root, &errors)) {
        throw std::runtime_error("unable to read H21 manual scoring samples: " + errors);
    }
    return root;
}

TEST(H21GradeAndControlsTest, GradeBoundariesComeFromPackage) {
    auto evaluator = bridge_report::tests::h21::evaluator();

    EXPECT_EQ(evaluator.classify_grade(100.0), 1);
    EXPECT_EQ(evaluator.classify_grade(95.0), 1);
    EXPECT_EQ(evaluator.classify_grade(94.999), 2);
    EXPECT_EQ(evaluator.classify_grade(80.0), 2);
    EXPECT_EQ(evaluator.classify_grade(60.0), 3);
    EXPECT_EQ(evaluator.classify_grade(40.0), 4);
    EXPECT_EQ(evaluator.classify_grade(0.0), 5);
    EXPECT_FALSE(evaluator.classify_grade(-0.01).has_value());
    EXPECT_FALSE(evaluator.classify_grade(100.01).has_value());
}

TEST(H21GradeAndControlsTest, TriggeredGrade5ControlOverridesCalculatedGrade) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    input.triggered_control_ids = {"h21.control.5.roof_or_deck_crack"};
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    EXPECT_EQ(outcome.result->calculated_grade, 1);
    EXPECT_EQ(outcome.result->final_grade, 5);
    ASSERT_EQ(outcome.result->triggered_controls.size(), 1u);
    EXPECT_EQ(outcome.result->triggered_controls.front().result_grade, 5);
}

TEST(H21GradeAndControlsTest, Grade3CombinationOverridesScoreGrade4) {
    const auto package = bridge_report::tests::h21::load_package();
    const auto fixture = manual_cases();
    ASSERT_EQ(fixture["standard"].asString(), "JTG/T H21—2011");
    ASSERT_EQ(fixture["package_version"].asString(), "1.0.1");
    const auto& test_case = fixture["cases"][1];
    ASSERT_EQ(test_case["name"].asString(), "grade_3_combination_control");
    bridge_report::standards::BridgeAssessmentInput input;
    input.bridge_type_id = "h21.bridge_type.beam";
    for (const auto& item : test_case["components"]) {
        input.components.push_back({
            item["instance_id"].asString(),
            item["component_type_id"].asString(),
            {{item["defect_indicator_id"].asString(), item["scale"].asInt()}},
        });
    }
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    EXPECT_DOUBLE_EQ(outcome.result->overall_score,
                     test_case["expected"]["overall"].asDouble());
    EXPECT_EQ(outcome.result->calculated_grade,
              test_case["expected"]["calculated_grade"].asInt());
    EXPECT_EQ(outcome.result->final_grade,
              test_case["expected"]["final_grade"].asInt());
    ASSERT_EQ(outcome.result->triggered_controls.size(), 1u);
    EXPECT_EQ(outcome.result->triggered_controls.front().control_id,
              test_case["expected"]["control_id"].asString());
}

TEST(H21GradeAndControlsTest, WorstUnsafeMajorCategoryCanControlFinalGrade) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.beam.upper_bearing").defects = {
        {"h21.defect.5_1_1_4", 4},
    };
    input.worst_major_component_affects_safety = true;
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    EXPECT_EQ(outcome.result->calculated_grade, 2);
    EXPECT_EQ(outcome.result->final_grade, 4);
    ASSERT_EQ(outcome.result->triggered_controls.size(), 1u);
    EXPECT_EQ(outcome.result->triggered_controls.front().control_id,
              "h21.control.worst_major_component");
}

TEST(H21GradeAndControlsTest, MissingRuleNeverFallsBackToPerfectScore) {
    auto package = bridge_report::tests::h21::load_package();
    package.definitions.erase("h21.deduction.scale_table.max_3");
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.beam.upper_bearing").defects = {
        {"h21.defect.5_1_1_1", 3},
    };
    bridge_report::standards::H21Evaluator evaluator(std::move(package));

    const auto outcome = evaluator.evaluate(input);

    EXPECT_FALSE(outcome.ok());
    EXPECT_FALSE(outcome.result.has_value());
    ASSERT_FALSE(outcome.issues.empty());
    EXPECT_EQ(outcome.issues.front().code, "rule_missing");
}

TEST(H21GradeAndControlsTest, RejectsUnsupportedInapplicableAndNonFiniteInputs) {
    const auto package = bridge_report::tests::h21::load_package();
    bridge_report::standards::H21Evaluator evaluator(package);

    auto unsupported = bridge_report::tests::h21::complete_beam_input(package);
    unsupported.bridge_type_id = "future.bridge.type";
    EXPECT_FALSE(evaluator.evaluate(unsupported).ok());

    auto inapplicable = bridge_report::tests::h21::complete_beam_input(package);
    component(inapplicable, "h21.component.deck.pavement").defects = {
        {"h21.defect.5_1_1_1", 3},
    };
    EXPECT_FALSE(evaluator.evaluate(inapplicable).ok());

    auto invalid_scale = bridge_report::tests::h21::complete_beam_input(package);
    component(invalid_scale, "h21.component.beam.upper_bearing").defects = {
        {"h21.defect.5_1_1_1", 4},
    };
    EXPECT_FALSE(evaluator.evaluate(invalid_scale).ok());
}

TEST(H21GradeAndControlsTest, RejectsIncompleteStructureHierarchyAndUnknownControl) {
    const auto package = bridge_report::tests::h21::load_package();
    bridge_report::standards::H21Evaluator evaluator(package);
    bridge_report::standards::BridgeAssessmentInput incomplete{
        "h21.bridge_type.beam",
        {{"beam-1", "h21.component.beam.upper_bearing", {}}},
        {},
        false,
    };
    EXPECT_FALSE(evaluator.evaluate(incomplete).ok());

    auto unknown_control = bridge_report::tests::h21::complete_beam_input(package);
    unknown_control.triggered_control_ids = {"h21.control.unknown"};
    EXPECT_FALSE(evaluator.evaluate(unknown_control).ok());
}

TEST(H21GradeAndControlsTest, RejectsDuplicateIndicatorOnSameComponent) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.beam.upper_bearing").defects = {
        {"h21.defect.5_1_1_1", 2},
        {"h21.defect.5_1_1_1", 3},
    };
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_FALSE(outcome.ok());
    EXPECT_TRUE(std::any_of(
        outcome.issues.begin(), outcome.issues.end(),
        [](const bridge_report::standards::AssessmentIssue& issue) {
            return issue.code == "defect_indicator_duplicate";
        }));
}

}  // namespace
