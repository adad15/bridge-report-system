#include <array>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/standards/Jtg5230Parameters.hpp"
#include "support/jtg5230_fixtures.hpp"

namespace {

using bridge_report::standards::Jtg5230ParameterTable;
using bridge_report::standards::Jtg5230Weight;
using bridge_report::standards::LayeredStructure;
using bridge_report::standards::MainComponentDesignation;
using bridge_report::standards::StandardIssue;
using bridge_report::standards::StandardPackage;
using bridge_report::standards::parse_jtg5230_parameters;
namespace jtg5230 = bridge_report::tests::jtg5230;

Json::Value& definition(StandardPackage& package, const std::string& file, const std::string& id) {
    for (auto& item : package.documents.at(file)["definitions"]) {
        if (item["id"].asString() == id) {
            return item;
        }
    }
    throw std::runtime_error("definition not found: " + id);
}

Json::Value& table_definition(StandardPackage& package, const std::string& table_suffix) {
    return definition(package, "parameter-tables.json", "jtg5230.parameter_table." + table_suffix);
}

Json::Value& weight_entry(Json::Value& table, const std::string& set_id, const std::string& part_suffix) {
    for (auto& weight_set : table["weight_sets"]) {
        if (weight_set["id"].asString() != set_id) {
            continue;
        }
        for (auto& entry : weight_set["weights"]) {
            if (entry["part_id"].asString() == jtg5230::part(part_suffix)) {
                return entry;
            }
        }
    }
    throw std::runtime_error("weight not found: " + part_suffix);
}

void expect_issue(
    const StandardPackage& package,
    const std::string& code,
    const std::vector<std::string>& message_fragments = {}) {
    const auto result = parse_jtg5230_parameters(package);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.parameters.has_value());
    const StandardIssue* found = nullptr;
    for (const auto& issue : result.issues) {
        if (issue.code == code) {
            found = &issue;
            break;
        }
    }
    ASSERT_NE(found, nullptr) << "缺少问题代码 " << code;
    for (const auto& fragment : message_fragments) {
        EXPECT_NE(found->message.find(fragment), std::string::npos) << found->message;
    }
}

std::array<int, 4> gammas(const Jtg5230ParameterTable& table) {
    return {
        table.gamma_hundredths.at(LayeredStructure::deck_system),
        table.gamma_hundredths.at(LayeredStructure::superstructure),
        table.gamma_hundredths.at(LayeredStructure::substructure),
        table.gamma_hundredths.at(LayeredStructure::ancillary),
    };
}

const Jtg5230Weight* weight_of(
    const Jtg5230ParameterTable& table,
    const std::string& set_id,
    const std::string& part_suffix) {
    const auto* weight_set = table.find_weight_set(set_id);
    if (weight_set == nullptr) {
        return nullptr;
    }
    for (const auto& weight : weight_set->weights) {
        if (weight.part_id == jtg5230::part(part_suffix)) {
            return &weight;
        }
    }
    return nullptr;
}

int omega_of(const Jtg5230ParameterTable& table, const std::string& set_id, const std::string& part_suffix) {
    const auto* weight = weight_of(table, set_id, part_suffix);
    return weight == nullptr ? -1 : weight->omega;
}

TEST(Jtg5230ParametersTest, ParsesRepositoryPackage) {
    const auto result = parse_jtg5230_parameters(jtg5230::load_package());

    ASSERT_TRUE(result.ok()) << (result.issues.empty() ? "" : result.issues.front().message);
    const auto& parameters = *result.parameters;
    EXPECT_EQ(parameters.parts.size(), 38u);
    EXPECT_EQ(parameters.controls.size(), 16u);
    const std::array<std::pair<int, std::string>, 6> grades{{
        {1, "优"}, {1, "优"}, {2, "良"}, {3, "中"}, {4, "次"}, {5, "差"},
    }};
    for (std::size_t eci = 0; eci < grades.size(); ++eci) {
        SCOPED_TRACE(eci);
        EXPECT_EQ(parameters.component_grades[eci].grade, grades[eci].first);
        EXPECT_EQ(parameters.component_grades[eci].state, grades[eci].second);
    }
    EXPECT_EQ(parameters.index_limit_tenths, (std::map<int, int>{{2, 900}, {3, 750}, {4, 600}, {5, 400}}));
    EXPECT_EQ(parameters.maintenance_strategies.at(2), (std::vector<std::string>{"日常养护", "预防养护"}));
    EXPECT_EQ(parameters.maintenance_strategies.at(4), (std::vector<std::string>{"修复养护"}));
    EXPECT_EQ(parameters.designation("beam", jtg5230::part("super.slab")), MainComponentDesignation::main);
    EXPECT_EQ(parameters.designation("beam", jtg5230::part("super.deck_slab")), MainComponentDesignation::not_main);
    EXPECT_EQ(
        parameters.designation("arch", jtg5230::part("super.spandrel_pier_and_wall")),
        MainComponentDesignation::mixed);
    EXPECT_EQ(
        parameters.designation("combined_arch", jtg5230::part("super.lateral_bracing")),
        MainComponentDesignation::main);
    EXPECT_EQ(parameters.designation("arch", jtg5230::part("super.lateral_bracing")), MainComponentDesignation::not_main);
    EXPECT_EQ(
        parameters.designation("suspension", jtg5230::part("super.bearing_and_restraint")),
        MainComponentDesignation::mixed);
}

TEST(Jtg5230ParametersTest, ClassifiesSpanIndexAtBandEdges) {
    const auto parameters = *parse_jtg5230_parameters(jtg5230::load_package()).parameters;
    const std::vector<std::pair<int, int>> cases{
        {1000, 1}, {901, 1}, {900, 2}, {751, 2}, {750, 3}, {600, 4}, {401, 4}, {400, 5}, {0, 5},
    };

    for (const auto& [index_tenths, grade] : cases) {
        SCOPED_TRACE(index_tenths);
        EXPECT_EQ(parameters.classify_span(index_tenths), grade);
    }
}

TEST(Jtg5230ParametersTest, SampleTablesMatchPrintedValues) {
    const auto parameters = *parse_jtg5230_parameters(jtg5230::load_package()).parameters;
    const auto& tables = parameters.parameter_tables;

    // 表 B.2.2，PDF 第 99 页
    const auto& b22 = tables.at("jtg5230.parameter_table.b_2_2");
    EXPECT_EQ(gammas(b22), (std::array<int, 4>{15, 40, 40, 5}));
    EXPECT_EQ(omega_of(b22, "standard", "super.slab"), 75);
    EXPECT_EQ(omega_of(b22, "standard", "super.bearing"), 25);
    EXPECT_EQ(omega_of(b22, "standard", "deck.pavement"), 40);
    EXPECT_EQ(omega_of(b22, "standard", "deck.expansion_joint"), 25);
    EXPECT_EQ(omega_of(b22, "standard", "sub.pier_or_abutment"), 60);
    EXPECT_EQ(omega_of(b22, "standard", "sub.foundation"), 40);
    EXPECT_EQ(omega_of(b22, "standard", "ancillary.riverbed"), 35);

    // 表 B.2.4，PDF 第 100 页
    const auto& b24 = tables.at("jtg5230.parameter_table.b_2_4");
    EXPECT_EQ(gammas(b24), (std::array<int, 4>{15, 42, 38, 5}));
    EXPECT_EQ(omega_of(b24, "standard", "super.main_girder"), 55);
    EXPECT_EQ(omega_of(b24, "standard", "super.diaphragm"), 20);
    EXPECT_EQ(omega_of(b24, "standard", "super.bearing"), 25);

    // 表 B.3.3，PDF 第 102～103 页
    const auto& b33 = tables.at("jtg5230.parameter_table.b_3_3");
    EXPECT_EQ(gammas(b33), (std::array<int, 4>{15, 45, 35, 5}));
    ASSERT_EQ(b33.weight_sets.size(), 2u);
    EXPECT_EQ(b33.weight_sets[0].id, "with_expansion_joint");
    EXPECT_EQ(b33.weight_sets[1].id, "without_expansion_joint");
    EXPECT_EQ(omega_of(b33, "with_expansion_joint", "deck.pavement"), 40);
    EXPECT_EQ(omega_of(b33, "with_expansion_joint", "deck.expansion_joint"), 25);
    EXPECT_EQ(omega_of(b33, "with_expansion_joint", "deck.railing_and_sidewalk"), 20);
    EXPECT_EQ(omega_of(b33, "with_expansion_joint", "deck.drainage"), 15);
    EXPECT_EQ(omega_of(b33, "without_expansion_joint", "deck.pavement"), 50);
    EXPECT_EQ(weight_of(b33, "without_expansion_joint", "deck.pavement")->j, 1);
    EXPECT_EQ(omega_of(b33, "without_expansion_joint", "deck.railing_and_sidewalk"), 30);
    EXPECT_EQ(weight_of(b33, "without_expansion_joint", "deck.railing_and_sidewalk")->j, 2);
    EXPECT_EQ(omega_of(b33, "without_expansion_joint", "deck.drainage"), 20);
    EXPECT_EQ(weight_of(b33, "without_expansion_joint", "deck.drainage")->j, 3);
    EXPECT_EQ(weight_of(b33, "without_expansion_joint", "deck.expansion_joint"), nullptr);
    for (const auto* set_id : {"with_expansion_joint", "without_expansion_joint"}) {
        SCOPED_TRACE(set_id);
        EXPECT_EQ(omega_of(b33, set_id, "super.main_arch_ring"), 55);
        EXPECT_EQ(omega_of(b33, set_id, "super.spandrel_pier_and_wall"), 20);
        EXPECT_EQ(omega_of(b33, set_id, "super.spandrel_arch_or_deck_slab"), 25);
        EXPECT_EQ(omega_of(b33, set_id, "sub.pier_or_abutment"), 60);
        EXPECT_EQ(omega_of(b33, set_id, "sub.foundation"), 40);
    }

    // 表 B.2.4 一表两用
    EXPECT_EQ(
        parameters.evaluation_types.at("jtg5230.evaluation_type.beam.precast_composite_box").parameter_table_id,
        "jtg5230.parameter_table.b_2_4");
    EXPECT_EQ(
        parameters.evaluation_types.at("jtg5230.evaluation_type.beam.precast_ribbed").parameter_table_id,
        "jtg5230.parameter_table.b_2_4");
}

TEST(Jtg5230ParametersTest, RejectsGammaNotSummingToOne) {
    auto package = jtg5230::load_package();
    table_definition(package, "b_2_2")["structures"][1]["gamma"] = 0.45;

    expect_issue(package, "parameters_sum_invalid", {"B.2.2"});
}

TEST(Jtg5230ParametersTest, RejectsGammaWithThreeDecimals) {
    auto package = jtg5230::load_package();
    table_definition(package, "b_2_2")["structures"][1]["gamma"] = 0.405;

    expect_issue(package, "parameters_field_invalid", {"B.2.2"});
}

TEST(Jtg5230ParametersTest, RejectsOmegaNotSummingToHundred) {
    auto package = jtg5230::load_package();
    weight_entry(table_definition(package, "b_2_2"), "standard", "super.bearing")["omega"] = 20;

    expect_issue(package, "parameters_sum_invalid", {"B.2.2", "上部结构"});
}

TEST(Jtg5230ParametersTest, RejectsUnknownPartInWeightSet) {
    auto package = jtg5230::load_package();
    weight_entry(table_definition(package, "b_2_2"), "standard", "super.bearing")["part_id"] =
        "jtg5230.part.super.not_a_part";

    expect_issue(package, "parameters_reference_unknown", {"B.2.2"});
}

TEST(Jtg5230ParametersTest, RejectsPartRepeatedInWeightSet) {
    auto package = jtg5230::load_package();
    weight_entry(table_definition(package, "b_2_2"), "standard", "super.bearing")["part_id"] =
        jtg5230::part("super.slab");

    expect_issue(package, "parameters_field_invalid", {"B.2.2"});
}

TEST(Jtg5230ParametersTest, RejectsEvaluationTypeWithUnknownTable) {
    auto package = jtg5230::load_package();
    definition(package, "evaluation-types.json", "jtg5230.evaluation_type.beam.precast_ribbed")
        ["parameter_table_id"] = "jtg5230.parameter_table.b_9_9";

    expect_issue(package, "parameters_reference_unknown", {"jtg5230.evaluation_type.beam.precast_ribbed"});
}

TEST(Jtg5230ParametersTest, RejectsEvaluationTypeWithUnknownFamily) {
    auto package = jtg5230::load_package();
    definition(package, "evaluation-types.json", "jtg5230.evaluation_type.beam.precast_ribbed")["family"] =
        "viaduct";

    expect_issue(package, "parameters_reference_unknown", {"jtg5230.evaluation_type.beam.precast_ribbed"});
}

TEST(Jtg5230ParametersTest, RejectsComponentClassificationGap) {
    auto package = jtg5230::load_package();
    auto& grades = definition(package, "classification.json", "jtg5230.classification.component")["grades"];
    Json::Value removed;
    grades.removeIndex(3, &removed);  // ECI 3 那一档

    expect_issue(package, "parameters_coverage_invalid", {"3.2.6"});
}

TEST(Jtg5230ParametersTest, RejectsOverlappingSpanBands) {
    auto package = jtg5230::load_package();
    // 2 类下界改成含端点后，75.0 同时落进 2 类和 3 类。
    definition(package, "classification.json", "jtg5230.classification.span")["grades"][1]["lower_inclusive"] =
        true;

    expect_issue(package, "parameters_coverage_invalid", {"3.3.2"});
}

TEST(Jtg5230ParametersTest, RejectsMissingMaintenanceGrade) {
    auto package = jtg5230::load_package();
    auto& strategies = definition(package, "maintenance-strategies.json", "jtg5230.maintenance.component")["strategies"];
    Json::Value removed;
    strategies.removeIndex(4, &removed);  // 5 类

    expect_issue(package, "parameters_coverage_invalid", {"3.5.2"});
}

TEST(Jtg5230ParametersTest, RejectsMissingDocument) {
    auto package = jtg5230::load_package();
    package.documents.erase("parts.json");

    expect_issue(package, "parameters_document_missing", {"parts.json"});
}

}  // namespace
