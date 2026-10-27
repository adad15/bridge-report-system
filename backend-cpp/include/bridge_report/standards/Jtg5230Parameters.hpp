#pragma once

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "bridge_report/standards/StandardModels.hpp"

namespace bridge_report::standards {

// JTG/T 5230—2026 把桥梁分为四个结构；附属构造是相对 H21 新增的一块，
// 所以不复用 H21 的 StructurePart。
enum class LayeredStructure {
    deck_system,
    superstructure,
    substructure,
    ancillary,
};

std::string to_string(LayeredStructure structure);
std::optional<LayeredStructure> parse_layered_structure(std::string_view value);
// 中文名：桥面系、上部结构、下部结构、附属构造。
std::string display_name(LayeredStructure structure);

// 表 3.3.3 按构件列主要构件，附录 B 按部件分权重；有的部件只有一部分构件是主要构件（mixed）。
enum class MainComponentDesignation {
    main,
    mixed,
    not_main,
};

struct Jtg5230Part {
    std::string id;
    std::string name;
    LayeredStructure structure{LayeredStructure::superstructure};
};

struct Jtg5230Weight {
    std::string part_id;
    std::string table_name;  // 表内原名
    int j{0};
    int omega{0};
};

struct Jtg5230WeightSet {
    std::string id;
    std::string label;
    std::vector<Jtg5230Weight> weights;  // 按表内 j 顺序
};

struct Jtg5230ParameterTable {
    std::string id;
    std::string source_table;
    std::vector<int> pdf_pages;
    std::map<LayeredStructure, int> gamma_hundredths;  // 恰好四个结构，合计 100
    std::vector<Jtg5230WeightSet> weight_sets;         // 至少一套
    std::vector<std::string> notes;

    const Jtg5230WeightSet* find_weight_set(const std::string& id) const;
};

struct Jtg5230EvaluationType {
    std::string id;
    std::string family;  // beam / arch / combined_arch / suspension / cable_stayed
    std::string name;
    std::string parameter_table_id;
};

struct Jtg5230MainComponents {
    std::set<std::string> main_part_ids;
    std::map<std::string, std::string> mixed_parts;  // 部件 id → 其中哪些构件是主要构件
};

struct Jtg5230ComponentGrade {
    int grade{0};
    std::string state;
};

struct Jtg5230SpanGradeBand {
    int grade{0};
    int lower_tenths{0};
    bool lower_inclusive{false};
    int upper_tenths{0};
    bool upper_inclusive{false};
};

struct Jtg5230Control {
    std::string id;
    std::string source_clause;
    std::string label;
    int result_grade{5};
};

struct Jtg5230Parameters {
    std::map<std::string, Jtg5230Part> parts;
    std::map<std::string, Jtg5230ParameterTable> parameter_tables;
    std::map<std::string, Jtg5230EvaluationType> evaluation_types;
    std::map<std::string, Jtg5230MainComponents> main_components;     // 键为桥型族
    std::array<Jtg5230ComponentGrade, 6> component_grades{};          // 下标为 ECI
    std::vector<Jtg5230SpanGradeBand> span_grades;
    std::map<int, int> index_limit_tenths;                            // 等级 → 上限×10
    std::map<int, std::vector<std::string>> maintenance_strategies;   // ECL → 养护对策
    std::map<std::string, Jtg5230Control> controls;

    // 族不存在时返回 not_main；评分器的 validate 会先报出未知族。
    MainComponentDesignation designation(const std::string& family, const std::string& part_id) const;
    // 0～1000（十分位）；解析时已保证每个十分位恰好落在一档里。
    int classify_span(int index_tenths) const;
};

struct Jtg5230ParametersResult {
    std::optional<Jtg5230Parameters> parameters;
    std::vector<StandardIssue> issues;

    bool ok() const noexcept { return parameters.has_value() && issues.empty(); }
};

// 解析规则包并做一致性校验（设计 3.4）。问题一次报全；只要有问题，parameters 就为空。
Jtg5230ParametersResult parse_jtg5230_parameters(const StandardPackage& package);

}  // namespace bridge_report::standards
