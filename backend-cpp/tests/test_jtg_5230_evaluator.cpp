#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/standards/Jtg5230Evaluator.hpp"
#include "support/jtg5230_fixtures.hpp"

namespace {

using bridge_report::standards::Jtg5230Evaluator;
using bridge_report::standards::LayeredAssessmentInput;
using bridge_report::standards::LayeredAssessmentResult;
using bridge_report::standards::LayeredComponentInput;
using bridge_report::standards::LayeredComponentResult;
namespace jtg5230 = bridge_report::tests::jtg5230;

const Jtg5230Evaluator& shared_evaluator() {
    static const auto instance = jtg5230::evaluator();
    return *instance;
}

LayeredAssessmentResult evaluate(const LayeredAssessmentInput& input) {
    const auto outcome = shared_evaluator().evaluate(input);
    if (!outcome.ok()) {
        ADD_FAILURE() << (outcome.issues.empty() ? std::string("评定没有结果")
                                                 : outcome.issues.front().code + "：" +
                                                       outcome.issues.front().message);
        return {};
    }
    return *outcome.result;
}

// 单跨整体式板桥（表 B.2.2）：单元 U1，桥跨 S1。
LayeredAssessmentInput slab_bridge(std::vector<LayeredComponentInput> components) {
    LayeredAssessmentInput input;
    input.units.push_back(jtg5230::unit("U1", jtg5230::kCastInPlaceSlab, "standard", {"S1"}));
    input.components = std::move(components);
    return input;
}

// 单跨空腹式板拱（表 B.3.3，设伸缩装置）：单元 A，桥跨 A1。
LayeredAssessmentInput open_spandrel_arch(std::vector<LayeredComponentInput> components) {
    LayeredAssessmentInput input;
    input.units.push_back(
        jtg5230::unit("A", jtg5230::kOpenSpandrelSlabArch, "with_expansion_joint", {"A1"}));
    input.components = std::move(components);
    return input;
}

const LayeredComponentResult& component_result(const LayeredAssessmentResult& result, const std::string& id) {
    for (const auto& component : result.components) {
        if (component.component_id == id) {
            return component;
        }
    }
    throw std::runtime_error("component result not found: " + id);
}

TEST(Jtg5230EvaluatorTest, CombinesDefectFunctionAndImpactScales) {
    auto slab = jtg5230::component("C1", "super.slab", {"S1"});
    slab.eddi = 1;
    slab.esdi = 2;
    slab.efi = 1;
    slab.eai = 1;

    const auto result = evaluate(slab_bridge({slab}));

    const auto& component = component_result(result, "C1");
    EXPECT_TRUE(component.is_main);
    EXPECT_EQ(component.edi, 2);
    EXPECT_EQ(component.eci, 4);
    EXPECT_EQ(component.ecl, 4);
    EXPECT_EQ(component.state, "次");
    EXPECT_EQ(component.maintenance_strategies, (std::vector<std::string>{"修复养护"}));
}

TEST(Jtg5230EvaluatorTest, CapsScaleAtFive) {
    auto slab = jtg5230::component("C1", "super.slab", {"S1"});
    slab.esdi = 3;
    slab.efi = 2;
    slab.eai = 1;

    const auto result = evaluate(slab_bridge({slab}));

    EXPECT_EQ(component_result(result, "C1").eci, 5);
    EXPECT_EQ(component_result(result, "C1").state, "差");
}

TEST(Jtg5230EvaluatorTest, FloorsScaleAtZero) {
    auto slab = jtg5230::component("C1", "super.slab", {"S1"});
    slab.eai = -1;

    const auto result = evaluate(slab_bridge({slab}));

    const auto& component = component_result(result, "C1");
    EXPECT_EQ(component.eci, 0);
    EXPECT_EQ(component.ecl, 1);
    EXPECT_EQ(component.state, "优");
    EXPECT_EQ(component.maintenance_strategies, (std::vector<std::string>{"日常养护"}));
}

TEST(Jtg5230EvaluatorTest, MapsEveryScaleToGradeStateAndStrategies) {
    std::vector<LayeredComponentInput> railings;
    for (int eci = 0; eci <= 5; ++eci) {
        railings.push_back(jtg5230::component_with_eci(
            "R" + std::to_string(eci), "deck.railing_and_sidewalk", {"S1"}, eci));
    }

    const auto result = evaluate(slab_bridge(railings));

    const std::vector<int> grades{1, 1, 2, 3, 4, 5};
    const std::vector<std::string> states{"优", "优", "良", "中", "次", "差"};
    for (int eci = 0; eci <= 5; ++eci) {
        SCOPED_TRACE(eci);
        const auto& component = component_result(result, "R" + std::to_string(eci));
        EXPECT_EQ(component.eci, eci);
        EXPECT_EQ(component.ecl, grades[static_cast<std::size_t>(eci)]);
        EXPECT_EQ(component.state, states[static_cast<std::size_t>(eci)]);
    }
    EXPECT_EQ(
        component_result(result, "R2").maintenance_strategies,
        (std::vector<std::string>{"日常养护", "预防养护"}));
    EXPECT_EQ(
        component_result(result, "R3").maintenance_strategies,
        (std::vector<std::string>{"预防养护", "修复养护"}));
}

TEST(Jtg5230EvaluatorTest, MissingComponentTakesDefectScaleThree) {
    auto joint = jtg5230::component("J1", "deck.expansion_joint", {"S1"});
    joint.missing = true;
    joint.eddi = 1;

    const auto result = evaluate(slab_bridge({joint}));

    EXPECT_EQ(component_result(result, "J1").edi, 3);
    EXPECT_EQ(component_result(result, "J1").eci, 3);
}

TEST(Jtg5230EvaluatorTest, IgnoresImpactScaleOnNonMainComponent) {
    auto railing = jtg5230::component("R1", "deck.railing_and_sidewalk", {"S1"});
    railing.eddi = 1;
    railing.eai = 1;

    const auto result = evaluate(slab_bridge({railing}));

    const auto& component = component_result(result, "R1");
    EXPECT_FALSE(component.is_main);
    EXPECT_EQ(component.eci, 1);
    EXPECT_TRUE(component.eai_ignored);
}

TEST(Jtg5230EvaluatorTest, MixedPartFollowsExplicitMainFlag) {
    auto spandrel = jtg5230::component("P1", "super.spandrel_pier_and_wall", {"A1"});
    spandrel.eddi = 1;
    spandrel.eai = 1;

    spandrel.is_main = true;
    const auto as_main = evaluate(open_spandrel_arch({spandrel}));
    spandrel.is_main = false;
    const auto as_other = evaluate(open_spandrel_arch({spandrel}));

    EXPECT_TRUE(component_result(as_main, "P1").is_main);
    EXPECT_EQ(component_result(as_main, "P1").eci, 2);
    EXPECT_FALSE(component_result(as_other, "P1").is_main);
    EXPECT_EQ(component_result(as_other, "P1").eci, 1);
    EXPECT_TRUE(component_result(as_other, "P1").eai_ignored);
}

}  // namespace
