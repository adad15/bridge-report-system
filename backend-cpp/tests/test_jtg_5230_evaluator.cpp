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
using bridge_report::standards::LayeredPartResult;
using bridge_report::standards::LayeredSpanResult;
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

// 整体式板桥（表 B.2.2）：单元 U1，默认单跨 S1。
LayeredAssessmentInput slab_bridge(
    std::vector<LayeredComponentInput> components,
    std::vector<std::string> spans = {"S1"}) {
    LayeredAssessmentInput input;
    input.units.push_back(jtg5230::unit("U1", jtg5230::kCastInPlaceSlab, "standard", std::move(spans)));
    input.components = std::move(components);
    return input;
}

// 单跨空腹式板拱（表 B.3.3）：单元 A，桥跨 A1。
LayeredAssessmentInput open_spandrel_arch(
    std::vector<LayeredComponentInput> components,
    std::string weight_set_id = "with_expansion_joint") {
    LayeredAssessmentInput input;
    input.units.push_back(jtg5230::unit("A", jtg5230::kOpenSpandrelSlabArch, std::move(weight_set_id), {"A1"}));
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

const LayeredSpanResult& span_result(const LayeredAssessmentResult& result, const std::string& span_id) {
    for (const auto& unit : result.units) {
        for (const auto& span : unit.spans) {
            if (span.span_id == span_id) {
                return span;
            }
        }
    }
    throw std::runtime_error("span result not found: " + span_id);
}

const LayeredPartResult* part_result(const LayeredSpanResult& span, const std::string& part_suffix) {
    for (const auto& part : span.parts) {
        if (part.part_id == jtg5230::part(part_suffix)) {
            return &part;
        }
    }
    return nullptr;
}

LayeredComponentInput with_eci(std::string id, std::string_view part_suffix, int eci) {
    return jtg5230::component_with_eci(std::move(id), part_suffix, {"S1"}, eci);
}

LayeredComponentInput affecting_safety(LayeredComponentInput component) {
    component.affects_bridge_safety = true;
    return component;
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

// 以下桥跨用例都用整体式板桥（表 B.2.2）。每 1 级 ECI 扣的十分位（γ 百分数 × ω ÷ 50）：
// 板 60、支座 20、桥面铺装 12、伸缩装置 7.5、护栏 6、排水 4.5、桥墩或桥台 48、墩台基础 32、
// 河床 3.5、翼墙 3.5、其他附属设施 3。

TEST(Jtg5230EvaluatorTest, TakesWorstComponentOfEachPart) {
    const auto result = evaluate(slab_bridge({with_eci("C1", "super.slab", 1), with_eci("C2", "super.slab", 3)}));

    const auto& span = span_result(result, "S1");
    ASSERT_NE(part_result(span, "super.slab"), nullptr);
    EXPECT_EQ(part_result(span, "super.slab")->ci, 3);
    EXPECT_EQ(part_result(span, "super.slab")->worst_component_id, "C2");
    EXPECT_DOUBLE_EQ(span.calculated_index, 82.0);
    EXPECT_EQ(span.calculated_grade, 2);
}

TEST(Jtg5230EvaluatorTest, SharedComponentCountsInBothSpans) {
    const auto pier = jtg5230::component_with_eci("P1", "sub.pier_or_abutment", {"S1", "S2"}, 2);

    const auto result = evaluate(slab_bridge({pier}, {"S1", "S2"}));

    for (const auto* span_id : {"S1", "S2"}) {
        SCOPED_TRACE(span_id);
        EXPECT_DOUBLE_EQ(span_result(result, span_id).calculated_index, 90.4);
        EXPECT_EQ(span_result(result, span_id).calculated_grade, 1);
    }
}

TEST(Jtg5230EvaluatorTest, SpanWithoutComponentsScoresHundred) {
    const auto result = evaluate(slab_bridge({with_eci("C1", "super.slab", 1)}, {"S1", "S2"}));

    const auto& span = span_result(result, "S2");
    EXPECT_DOUBLE_EQ(span.calculated_index, 100.0);
    EXPECT_EQ(span.calculated_grade, 1);
    EXPECT_EQ(span.parts.size(), 11u);
    for (const auto& part : span.parts) {
        SCOPED_TRACE(part.part_id);
        EXPECT_EQ(part.ci, 0);
        EXPECT_TRUE(part.worst_component_id.empty());
    }
}

TEST(Jtg5230EvaluatorTest, ClassifiesBandEdges) {
    struct EdgeCase {
        std::vector<LayeredComponentInput> components;
        double index;
        int grade;
    };
    const std::vector<EdgeCase> cases{
        {{with_eci("C1", "super.slab", 1), with_eci("C2", "super.bearing", 2)}, 90.0, 2},
        {{with_eci("C1", "super.slab", 3), with_eci("C2", "super.bearing", 2), with_eci("C3", "deck.pavement", 2),
          with_eci("C4", "deck.railing_and_sidewalk", 1)},
         75.0,
         3},
        {{with_eci("C1", "super.slab", 4), with_eci("C2", "sub.pier_or_abutment", 2),
          with_eci("C3", "sub.foundation", 2)},
         60.0,
         4},
        {{with_eci("C1", "super.slab", 5), with_eci("C2", "super.bearing", 5), with_eci("C3", "sub.pier_or_abutment", 3),
          with_eci("C4", "sub.foundation", 1), with_eci("C5", "deck.pavement", 2)},
         40.0,
         5},
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.index);
        const auto result = evaluate(slab_bridge(item.components));
        const auto& span = span_result(result, "S1");
        EXPECT_DOUBLE_EQ(span.calculated_index, item.index);
        EXPECT_EQ(span.calculated_grade, item.grade);
    }
}

TEST(Jtg5230EvaluatorTest, RoundsSpanIndexHalfUpInDecimal) {
    // 精确值 99.65；直接用 double 算是 99.6499…，会舍成 99.6。
    const auto result = evaluate(slab_bridge({with_eci("C1", "ancillary.riverbed", 1)}));

    EXPECT_DOUBLE_EQ(span_result(result, "S1").calculated_index, 99.7);
}

TEST(Jtg5230EvaluatorTest, RedistributesWeightOfPartsNotRequired) {
    auto input = slab_bridge({with_eci("C1", "deck.pavement", 3)});
    input.units[0].not_required_part_ids = {jtg5230::part("deck.expansion_joint")};

    const auto result = evaluate(input);

    const auto& span = span_result(result, "S1");

    EXPECT_DOUBLE_EQ(span.calculated_index, 95.2);
    ASSERT_NE(part_result(span, "deck.pavement"), nullptr);
    EXPECT_NEAR(part_result(span, "deck.pavement")->effective_omega, 53.333, 1e-3);
    EXPECT_EQ(part_result(span, "deck.expansion_joint"), nullptr);
}

TEST(Jtg5230EvaluatorTest, ExplicitWeightSetIsUsedAsPrinted) {
    auto pavement = jtg5230::component_with_eci("C1", "deck.pavement", {"A1"}, 3);

    const auto result = evaluate(open_spandrel_arch({pavement}, "without_expansion_joint"));

    const auto& span = span_result(result, "A1");
    EXPECT_DOUBLE_EQ(span.calculated_index, 95.5);
    ASSERT_NE(part_result(span, "deck.pavement"), nullptr);
    EXPECT_DOUBLE_EQ(part_result(span, "deck.pavement")->effective_omega, 50.0);
    EXPECT_EQ(part_result(span, "deck.expansion_joint"), nullptr);
}

TEST(Jtg5230EvaluatorTest, OverridesSpanWhenMainComponentIsGradeFourAndAffectsSafety) {
    const auto result = evaluate(slab_bridge({affecting_safety(with_eci("C1", "super.slab", 4))}));

    const auto& span = span_result(result, "S1");
    EXPECT_DOUBLE_EQ(span.calculated_index, 76.0);
    EXPECT_EQ(span.calculated_grade, 2);
    EXPECT_DOUBLE_EQ(span.index, 60.0);
    EXPECT_EQ(span.grade, 4);
    EXPECT_EQ(span.override_component_id, "C1");
}

TEST(Jtg5230EvaluatorTest, KeepsSpanWithoutSafetyFlag) {
    const auto result = evaluate(slab_bridge({with_eci("C1", "super.slab", 4)}));

    const auto& span = span_result(result, "S1");
    EXPECT_DOUBLE_EQ(span.index, 76.0);
    EXPECT_EQ(span.grade, 2);
    EXPECT_TRUE(span.override_component_id.empty());
}

TEST(Jtg5230EvaluatorTest, KeepsWorseCalculatedResult) {
    const auto result = evaluate(slab_bridge({
        affecting_safety(with_eci("C1", "super.slab", 5)),
        with_eci("C2", "super.bearing", 5),
        with_eci("C3", "sub.pier_or_abutment", 4),
        with_eci("C4", "sub.foundation", 2),
    }));

    const auto& span = span_result(result, "S1");
    EXPECT_DOUBLE_EQ(span.calculated_index, 34.4);
    EXPECT_EQ(span.calculated_grade, 5);
    EXPECT_DOUBLE_EQ(span.index, 34.4);
    EXPECT_EQ(span.grade, 5);
    EXPECT_TRUE(span.override_component_id.empty());
}

TEST(Jtg5230EvaluatorTest, NonMainComponentNeverOverrides) {
    const auto result = evaluate(slab_bridge({affecting_safety(with_eci("R1", "deck.railing_and_sidewalk", 5))}));

    const auto& span = span_result(result, "S1");
    EXPECT_DOUBLE_EQ(span.index, 97.0);
    EXPECT_EQ(span.grade, 1);
    EXPECT_TRUE(span.override_component_id.empty());
}

TEST(Jtg5230EvaluatorTest, MixedPartOverridesOnlyWhenMarkedMain) {
    auto spandrel = affecting_safety(jtg5230::component_with_eci("P1", "super.spandrel_pier_and_wall", {"A1"}, 4));

    spandrel.is_main = true;
    const auto as_main = evaluate(open_spandrel_arch({spandrel}));
    spandrel.is_main = false;
    const auto as_other = evaluate(open_spandrel_arch({spandrel}));

    EXPECT_EQ(span_result(as_main, "A1").grade, 4);
    EXPECT_DOUBLE_EQ(span_result(as_main, "A1").index, 60.0);
    EXPECT_EQ(span_result(as_main, "A1").override_component_id, "P1");
    EXPECT_EQ(span_result(as_other, "A1").grade, 1);
    EXPECT_DOUBLE_EQ(span_result(as_other, "A1").index, 92.8);
    EXPECT_TRUE(span_result(as_other, "A1").override_component_id.empty());
}

}  // namespace
