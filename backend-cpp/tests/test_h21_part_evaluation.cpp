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
    EXPECT_EQ(result.component_type_name, "上部承重构件（主梁、挂梁）");
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


// ---------- 两侧护栏：绑一侧与绑两侧的分差 ----------
//
// 百股大桥 BG-2024-02。报告写"两侧护栏"，系统只绑了左侧，右侧当作无病害留在
// 100 分，部件分算出 76；报告是 56。下面两条锁的是 H21 公式本身而不是绑定逻辑，
// 目的是让"56 从哪来"在代码里有据可查：指标 h21.defect.10_4_1_2 标度 3 扣 40，
// 构件分 100-40=60；两件构件的数量系数 t=10（规范表 4.1.2）。

TEST(H21PartEvaluationTest, RailingScoresSeventySixWhenOnlyOneSideCarriesTheDefect) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.deck.railing").defects = {{"h21.defect.10_4_1_2", 3}};
    // 另一侧存在于台账但没挂病害——这正是"只绑左侧"的后果。
    input.components.push_back({
        "h21.component.deck.railing.instance.2",
        "h21.component.deck.railing",
        {},
    });
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    const auto& result = category(*outcome.result, "h21.component.deck.railing");
    EXPECT_DOUBLE_EQ(result.mean_component_score, 80.0);
    EXPECT_DOUBLE_EQ(result.minimum_component_score, 60.0);
    // t 单独断言：t 表将来若被改动，这两条的结论会跟着变，不该悄悄变。
    ASSERT_TRUE(result.component_count_factor.has_value());
    EXPECT_DOUBLE_EQ(*result.component_count_factor, 10.0);
    EXPECT_DOUBLE_EQ(result.score, 76.0);
}

TEST(H21PartEvaluationTest, RailingScoresFiftySixWhenBothSidesCarryTheDefect) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.deck.railing").defects = {{"h21.defect.10_4_1_2", 3}};
    input.components.push_back({
        "h21.component.deck.railing.instance.2",
        "h21.component.deck.railing",
        {{"h21.defect.10_4_1_2", 3}},
    });
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    const auto& result = category(*outcome.result, "h21.component.deck.railing");
    EXPECT_DOUBLE_EQ(result.mean_component_score, 60.0);
    EXPECT_DOUBLE_EQ(result.minimum_component_score, 60.0);
    ASSERT_TRUE(result.component_count_factor.has_value());
    EXPECT_DOUBLE_EQ(*result.component_count_factor, 10.0);
    // 60 - (100-60)/10 = 56，与检测报告一致。
    EXPECT_DOUBLE_EQ(result.score, 56.0);
}

// 合成一件不是等价变换：单构件时 t=∞，部件分退化为构件分，最低分惩罚项消失。
// 记在这里是为了挡住"把两侧做成一件构件"这个看似省事的念头——它得 60 不是 56。
TEST(H21PartEvaluationTest, ASingleRailingComponentLosesTheMinimumScorePenalty) {
    const auto package = bridge_report::tests::h21::load_package();
    auto input = bridge_report::tests::h21::complete_beam_input(package);
    component(input, "h21.component.deck.railing").defects = {{"h21.defect.10_4_1_2", 3}};
    bridge_report::standards::H21Evaluator evaluator(package);

    const auto outcome = evaluator.evaluate(input);

    ASSERT_TRUE(outcome.ok());
    const auto& result = category(*outcome.result, "h21.component.deck.railing");
    EXPECT_FALSE(result.component_count_factor.has_value()) << "单构件的 t 是无穷";
    EXPECT_DOUBLE_EQ(result.score, 60.0);
}

}  // namespace
