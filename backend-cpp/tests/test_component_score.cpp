#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/ComponentScore.hpp"

using bridge_report::review::classify_score_validation;
using bridge_report::review::compute_component_score;
using bridge_report::review::round_score_to_two_decimals;

namespace {

Json::Value read_score_fixture() {
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT)
        / "samples" / "scoring" / "component_score_cases.json";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open fixture: " + path.string());
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors)) {
        throw std::runtime_error("Unable to parse fixture: " + path.string() + ": " + errors);
    }
    return root;
}

std::vector<double> to_doubles(const Json::Value& values) {
    std::vector<double> result;
    for (const auto& value : values) {
        result.push_back(value.asDouble());
    }
    return result;
}

}  // namespace

TEST(ComponentScoreTest, SharedFixtureCasesAllMatch) {
    const auto fixture = read_score_fixture();
    ASSERT_EQ(fixture["standard"].asString(), "JTG/T H21-2011 4.1.1");
    const auto& cases = fixture["cases"];
    ASSERT_GE(cases.size(), 10u);

    for (const auto& test_case : cases) {
        SCOPED_TRACE(test_case["name"].asString());
        const auto result = compute_component_score(to_doubles(test_case["deductions"]));
        if (test_case["expected_unrounded"].isNull()) {
            EXPECT_FALSE(result.has_value());
            continue;
        }
        ASSERT_TRUE(result.has_value());
        EXPECT_LT(std::abs(result->score - test_case["expected_unrounded"].asDouble()), 1e-9);
        EXPECT_EQ(round_score_to_two_decimals(result->score), test_case["expected_rounded"].asDouble());
        EXPECT_EQ(result->ordered_deductions, to_doubles(test_case["expected_ordered_deductions"]));
    }
}

TEST(ComponentScoreTest, KeyValuesMatchChangeProposal) {
    EXPECT_EQ(compute_component_score({35.0})->score, 65.0);

    const auto two = compute_component_score({35.0, 20.0});
    ASSERT_TRUE(two.has_value());
    EXPECT_EQ(round_score_to_two_decimals(two->score), 55.81);
    // 计算全程不提前舍入：未舍入值与公式手算逐位一致。
    const double manual = 100.0 - 35.0 - 20.0 / (100.0 * std::sqrt(2.0)) * 65.0;
    EXPECT_EQ(two->score, manual);

    EXPECT_EQ(compute_component_score({20.0, 35.0})->score, two->score);
    EXPECT_EQ(compute_component_score({100.0})->score, 0.0);
    EXPECT_EQ(compute_component_score({50.0, 100.0})->score, 0.0);
}

TEST(ComponentScoreTest, RejectsEmptyAndOutOfRangeDeductions) {
    EXPECT_FALSE(compute_component_score({}).has_value());
    EXPECT_FALSE(compute_component_score({0.0}).has_value());
    EXPECT_FALSE(compute_component_score({-5.0}).has_value());
    EXPECT_FALSE(compute_component_score({150.0}).has_value());
    EXPECT_FALSE(compute_component_score({35.0, 0.0}).has_value());
}

TEST(ComponentScoreTest, ClassifiesScoreValidation) {
    EXPECT_EQ(classify_score_validation(std::nullopt, std::nullopt), "无法复算");
    EXPECT_EQ(classify_score_validation(55.81, std::nullopt), "无法复算");
    EXPECT_EQ(classify_score_validation(std::nullopt, 55.80761184457488), "不一致");
    EXPECT_EQ(classify_score_validation(55.81, 55.80761184457488), "一致");
    EXPECT_EQ(classify_score_validation(65.0, 55.80761184457488), "不一致");
}

TEST(ComponentScoreTest, RoundingIsHalfAwayFromZero) {
    EXPECT_EQ(round_score_to_two_decimals(55.805), 55.81);
    EXPECT_EQ(round_score_to_two_decimals(55.8076118445748), 55.81);
    EXPECT_EQ(round_score_to_two_decimals(65.0), 65.0);
    EXPECT_EQ(round_score_to_two_decimals(0.125), 0.13);
    EXPECT_EQ(round_score_to_two_decimals(-1.005), -round_score_to_two_decimals(1.005));
}
