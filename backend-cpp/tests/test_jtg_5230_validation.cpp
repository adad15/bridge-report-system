#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/standards/Jtg5230Evaluator.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"
#include "bridge_report/standards/TechnicalConditionStandard.hpp"
#include "support/jtg5230_fixtures.hpp"

namespace {

using bridge_report::standards::AssessmentIssue;
using bridge_report::standards::Jtg5230Evaluator;
using bridge_report::standards::LayeredAssessmentInput;
using bridge_report::standards::LayeredComponentInput;
using bridge_report::standards::LayeredTechnicalConditionStandard;
using bridge_report::standards::LayeredUnitInput;
using bridge_report::standards::StandardIssue;
using bridge_report::standards::StandardRegistry;
using bridge_report::standards::TechnicalConditionStandard;
namespace jtg5230 = bridge_report::tests::jtg5230;

bool has_code(const std::vector<AssessmentIssue>& issues, const std::string& code) {
    return std::any_of(issues.begin(), issues.end(), [&](const AssessmentIssue& issue) {
        return issue.code == code;
    });
}

bool has_code(const std::vector<StandardIssue>& issues, const std::string& code) {
    return std::any_of(issues.begin(), issues.end(), [&](const StandardIssue& issue) {
        return issue.code == code;
    });
}

std::string describe(const std::vector<AssessmentIssue>& issues) {
    std::string text;
    for (const auto& issue : issues) {
        text += issue.code + "：" + issue.message + "\n";
    }
    return text;
}

// 两跨整体式板桥：板在第 1 跨，桥墩两跨共用。
LayeredAssessmentInput base_input() {
    LayeredAssessmentInput input;
    input.units.push_back(jtg5230::unit("U1", jtg5230::kCastInPlaceSlab, "standard", {"S1", "S2"}));
    auto slab = jtg5230::component("C1", "super.slab", {"S1"});
    slab.esdi = 2;
    auto pier = jtg5230::component("C2", "sub.pier_or_abutment", {"S1", "S2"});
    pier.eddi = 1;
    input.components = {slab, pier};
    return input;
}

LayeredUnitInput arch_unit() {
    return jtg5230::unit("U2", jtg5230::kOpenSpandrelSlabArch, "with_expansion_joint", {"A1"});
}

TEST(Jtg5230ValidationTest, CreatesEvaluatorFromRepositoryPackage) {
    const auto created = Jtg5230Evaluator::create(jtg5230::load_package());

    ASSERT_NE(created.evaluator, nullptr);
    EXPECT_TRUE(created.issues.empty());
    EXPECT_EQ(created.evaluator->metadata().standard_id, "JTG_T_5230_2026");
    EXPECT_EQ(created.evaluator->algorithm_id(), "jtg-t-5230-2026");
}

TEST(Jtg5230ValidationTest, RefusesPackageWithInconsistentParameters) {
    auto package = jtg5230::load_package();
    for (auto& table : package.documents.at("parameter-tables.json")["definitions"]) {
        if (table["source_table"].asString() == "B.2.2") {
            table["structures"][1]["gamma"] = 0.45;
        }
    }

    const auto created = Jtg5230Evaluator::create(package);

    EXPECT_EQ(created.evaluator, nullptr);
    EXPECT_TRUE(has_code(created.issues, "parameters_sum_invalid"));
}

TEST(Jtg5230ValidationTest, RefusesPackageOfAnotherAlgorithm) {
    auto package = jtg5230::load_package();
    package.manifest.algorithm_id = "jtg-h21-2011";

    const auto created = Jtg5230Evaluator::create(package);

    EXPECT_EQ(created.evaluator, nullptr);
    EXPECT_TRUE(has_code(created.issues, "algorithm_unsupported"));
}

TEST(Jtg5230ValidationTest, RegistryCreatesLayeredEvaluatorForDraftPackage) {
    auto package = jtg5230::load_package();
    const auto key = package.key();
    StandardRegistry registry;
    ASSERT_TRUE(registry.register_package(std::move(package)).accepted);

    const auto algorithm = registry.create_algorithm(key);

    ASSERT_NE(algorithm, nullptr);
    EXPECT_NE(dynamic_cast<LayeredTechnicalConditionStandard*>(algorithm.get()), nullptr);
    // H21 的调用方按 TechnicalConditionStandard 取评分器，取不到就判阻断，不会误用 5230。
    EXPECT_EQ(dynamic_cast<TechnicalConditionStandard*>(algorithm.get()), nullptr);
}

TEST(Jtg5230ValidationTest, ValidInputHasNoIssues) {
    const auto evaluator = jtg5230::evaluator();

    const auto issues = evaluator->validate(base_input());

    EXPECT_TRUE(issues.empty()) << describe(issues);
}

struct ValidationCase {
    std::string name;
    std::function<void(LayeredAssessmentInput&)> change;
    std::string code;
};

TEST(Jtg5230ValidationTest, ReportsEachInputProblem) {
    const auto evaluator = jtg5230::evaluator();
    const std::vector<ValidationCase> cases{
        {"没有单元", [](LayeredAssessmentInput& input) { input.units.clear(); }, "units_missing"},
        {"单元 id 重复",
         [](LayeredAssessmentInput& input) {
             input.units.push_back(jtg5230::unit("U1", jtg5230::kCastInPlaceSlab, "standard", {"S9"}));
         },
         "unit_id_invalid"},
        {"评定类型不存在",
         [](LayeredAssessmentInput& input) {
             input.units[0].evaluation_type_id = "jtg5230.evaluation_type.beam.nope";
         },
         "evaluation_type_unknown"},
        {"权重套不存在",
         [](LayeredAssessmentInput& input) { input.units[0].weight_set_id = "with_expansion_joint"; },
         "weight_set_unknown"},
        {"单元没有桥跨", [](LayeredAssessmentInput& input) { input.units[0].span_ids.clear(); }, "unit_spans_missing"},
        {"桥跨 id 重复",
         [](LayeredAssessmentInput& input) { input.units[0].span_ids = {"S1", "S1"}; },
         "span_id_invalid"},
        {"无须设置的部件不在权重套里",
         [](LayeredAssessmentInput& input) {
             input.units[0].not_required_part_ids = {jtg5230::part("super.tie_beam")};
         },
         "not_required_part_invalid"},
        {"有显式权重套可用",
         [](LayeredAssessmentInput& input) {
             auto arch = arch_unit();
             arch.not_required_part_ids = {jtg5230::part("deck.expansion_joint")};
             input.units.push_back(arch);
         },
         "explicit_weight_set_available"},
        {"一个结构的部件全部无须设置",
         [](LayeredAssessmentInput& input) {
             input.units[0].not_required_part_ids = {
                 jtg5230::part("ancillary.riverbed"),
                 jtg5230::part("ancillary.wing_wall_and_slope"),
                 jtg5230::part("ancillary.other_facilities"),
             };
         },
         "structure_without_parts"},
        {"构件 id 重复", [](LayeredAssessmentInput& input) { input.components[1].component_id = "C1"; }, "component_id_invalid"},
        {"构件没有桥跨",
         [](LayeredAssessmentInput& input) { input.components[0].span_ids.clear(); },
         "component_spans_missing"},
        {"构件的桥跨不存在",
         [](LayeredAssessmentInput& input) { input.components[0].span_ids = {"S9"}; },
         "component_span_unknown"},
        {"构件所在单元分属不同桥型族",
         [](LayeredAssessmentInput& input) {
             input.units.push_back(arch_unit());
             input.components[1].span_ids = {"S1", "A1"};
         },
         "component_spans_cross_families"},
        {"部件不在单元的权重套里",
         [](LayeredAssessmentInput& input) { input.components[0].part_id = jtg5230::part("super.tie_beam"); },
         "component_part_not_in_unit"},
        {"部件已列为无须设置",
         [](LayeredAssessmentInput& input) {
             input.units[0].not_required_part_ids = {jtg5230::part("deck.expansion_joint")};
             input.components.push_back(jtg5230::component("C3", "deck.expansion_joint", {"S1"}));
         },
         "component_part_not_required"},
        {"标度越界", [](LayeredAssessmentInput& input) { input.components[0].esdi = 4; }, "scale_out_of_range"},
        {"部分为主要构件的部件没标",
         [](LayeredAssessmentInput& input) {
             input.units.push_back(arch_unit());
             input.components.push_back(jtg5230::component("C4", "super.spandrel_pier_and_wall", {"A1"}));
         },
         "main_component_flag_required"},
        {"其他部件给了是否主要构件",
         [](LayeredAssessmentInput& input) { input.components[0].is_main = true; },
         "main_component_flag_not_applicable"},
        {"单项控制不存在",
         [](LayeredAssessmentInput& input) { input.triggered_control_ids = {"jtg5230.control.nope"}; },
         "single_item_control_unknown"},
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.name);
        auto input = base_input();
        item.change(input);
        const auto issues = evaluator->validate(input);
        EXPECT_TRUE(has_code(issues, item.code)) << describe(issues);
    }
}

TEST(Jtg5230ValidationTest, ExplicitWeightSetMessageNamesTheSetToUse) {
    const auto evaluator = jtg5230::evaluator();
    auto input = base_input();
    auto arch = arch_unit();
    arch.not_required_part_ids = {jtg5230::part("deck.expansion_joint")};
    input.units.push_back(arch);

    const auto issues = evaluator->validate(input);

    const auto found = std::find_if(issues.begin(), issues.end(), [](const AssessmentIssue& issue) {
        return issue.code == "explicit_weight_set_available";
    });
    ASSERT_NE(found, issues.end()) << describe(issues);
    EXPECT_NE(found->message.find("without_expansion_joint"), std::string::npos) << found->message;
    EXPECT_EQ(found->entity_id, jtg5230::part("deck.expansion_joint"));
}

TEST(Jtg5230ValidationTest, EveryPartiallyMainPartRequiresExplicitFlag) {
    struct FlagCase {
        std::string evaluation_type_id;
        std::string part_suffix;
    };
    const std::vector<FlagCase> cases{
        {"jtg5230.evaluation_type.arch.open_spandrel_slab", "super.spandrel_pier_and_wall"},
        {"jtg5230.evaluation_type.combined_arch.flexible_tie", "super.floor_system"},
        {"jtg5230.evaluation_type.suspension.self_anchored", "super.bearing_and_restraint"},
        {"jtg5230.evaluation_type.cable_stayed.full", "super.bearing_and_restraint"},
    };
    const auto evaluator = jtg5230::evaluator();
    const auto weight_set_of = [](const std::string& type_id) {
        return type_id.find(".arch.") != std::string::npos ? "with_expansion_joint" : "standard";
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.evaluation_type_id + " / " + item.part_suffix);
        LayeredAssessmentInput input;
        input.units.push_back(jtg5230::unit("U1", item.evaluation_type_id, weight_set_of(item.evaluation_type_id), {"S1"}));
        input.components.push_back(jtg5230::component("C1", item.part_suffix, {"S1"}));
        EXPECT_TRUE(has_code(evaluator->validate(input), "main_component_flag_required"));

        input.components[0].is_main = false;
        const auto issues = evaluator->validate(input);
        EXPECT_TRUE(issues.empty()) << describe(issues);
    }
}

TEST(Jtg5230ValidationTest, RejectsEachOutOfRangeScale) {
    const auto evaluator = jtg5230::evaluator();
    using Change = std::function<void(LayeredComponentInput&)>;
    const std::vector<std::pair<std::string, Change>> out_of_range{
        {"eddi=3", [](LayeredComponentInput& c) { c.eddi = 3; }},
        {"esdi=-1", [](LayeredComponentInput& c) { c.esdi = -1; }},
        {"efi=3", [](LayeredComponentInput& c) { c.efi = 3; }},
        {"eai=2", [](LayeredComponentInput& c) { c.eai = 2; }},
        {"eai=-2", [](LayeredComponentInput& c) { c.eai = -2; }},
    };
    const std::vector<std::pair<std::string, Change>> in_range{
        {"eddi=2", [](LayeredComponentInput& c) { c.eddi = 2; }},
        {"esdi=3", [](LayeredComponentInput& c) { c.esdi = 3; }},
        {"efi=2", [](LayeredComponentInput& c) { c.efi = 2; }},
        {"eai=-1", [](LayeredComponentInput& c) { c.eai = -1; }},
        {"eai=1", [](LayeredComponentInput& c) { c.eai = 1; }},
    };

    for (const auto& [name, change] : out_of_range) {
        SCOPED_TRACE(name);
        auto input = base_input();
        change(input.components[0]);
        EXPECT_TRUE(has_code(evaluator->validate(input), "scale_out_of_range"));
    }
    for (const auto& [name, change] : in_range) {
        SCOPED_TRACE(name);
        auto input = base_input();
        change(input.components[0]);
        EXPECT_FALSE(has_code(evaluator->validate(input), "scale_out_of_range"));
    }
}

TEST(Jtg5230ValidationTest, EvaluateReturnsIssuesWithoutResult) {
    const auto evaluator = jtg5230::evaluator();
    auto input = base_input();
    input.units.clear();

    const auto outcome = evaluator->evaluate(input);

    EXPECT_FALSE(outcome.ok());
    EXPECT_FALSE(outcome.result.has_value());
    EXPECT_TRUE(has_code(outcome.issues, "units_missing"));
}

}  // namespace
