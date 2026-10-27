#include "bridge_report/standards/Jtg5230Parameters.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

#include <json/json.h>

namespace bridge_report::standards {

namespace {

constexpr const char* kDocumentMissing = "parameters_document_missing";
constexpr const char* kFieldInvalid = "parameters_field_invalid";
constexpr const char* kReferenceUnknown = "parameters_reference_unknown";
constexpr const char* kSumInvalid = "parameters_sum_invalid";
constexpr const char* kCoverageInvalid = "parameters_coverage_invalid";

constexpr const char* kComponentClassificationId = "jtg5230.classification.component";
constexpr const char* kSpanClassificationId = "jtg5230.classification.span";
constexpr const char* kIndexLimitId = "jtg5230.classification.index_limit";
constexpr const char* kMaintenanceId = "jtg5230.maintenance.component";

constexpr std::array kStructures{
    LayeredStructure::deck_system,
    LayeredStructure::superstructure,
    LayeredStructure::substructure,
    LayeredStructure::ancillary,
};

std::string structure_label(LayeredStructure structure) {
    switch (structure) {
        case LayeredStructure::deck_system:
            return "桥面系";
        case LayeredStructure::superstructure:
            return "上部结构";
        case LayeredStructure::substructure:
            return "下部结构";
        case LayeredStructure::ancillary:
            return "附属构造";
    }
    return {};
}

// 百分数写回小数给人看：115 → "1.15"。
std::string hundredths_text(int value) {
    std::ostringstream text;
    text << value / 100 << '.' << std::setw(2) << std::setfill('0') << value % 100;
    return text.str();
}

// 十分位写回一位小数：750 → "75.0"。
std::string tenths_text(int value) {
    return std::to_string(value / 10) + "." + std::to_string(value % 10);
}

bool band_contains(const Jtg5230SpanGradeBand& band, int index_tenths) {
    const bool above_lower = index_tenths > band.lower_tenths ||
                             (band.lower_inclusive && index_tenths == band.lower_tenths);
    const bool below_upper = index_tenths < band.upper_tenths ||
                             (band.upper_inclusive && index_tenths == band.upper_tenths);
    return above_lower && below_upper;
}

class Parser {
public:
    explicit Parser(const StandardPackage& package) : package_(package) {}

    Jtg5230ParametersResult run() {
        parse_parts();
        parse_main_components();
        parse_parameter_tables();
        parse_evaluation_types();
        parse_classification();
        parse_controls();
        parse_maintenance();

        Jtg5230ParametersResult result;
        result.issues = std::move(issues_);
        if (result.issues.empty()) {
            result.parameters = std::move(parameters_);
        }
        return result;
    }

private:
    void add(const char* code, std::string message) {
        issues_.push_back({code, std::move(message)});
    }

    // 入口文件的 definitions 数组；缺文件时报一次并返回空指针。
    const Json::Value* definitions(const char* file) {
        const auto found = package_.documents.find(file);
        if (found == package_.documents.end()) {
            add(kDocumentMissing, std::string("规则包缺少入口文件 ") + file + "。");
            return nullptr;
        }
        const auto& items = found->second["definitions"];
        if (!items.isArray()) {
            add(kFieldInvalid, std::string(file) + " 的 definitions 必须是数组。");
            return nullptr;
        }
        return &items;
    }

    const Json::Value* required_definition(const Json::Value* items, const char* id) {
        if (items == nullptr) {
            return nullptr;
        }
        for (const auto& item : *items) {
            if (item.isObject() && item["id"].isString() && item["id"].asString() == id) {
                return &item;
            }
        }
        add(kFieldInvalid, std::string("规则包缺少定义 ") + id + "。");
        return nullptr;
    }

    bool object(const Json::Value& item, const std::string& where) {
        if (item.isObject()) {
            return true;
        }
        add(kFieldInvalid, where + " 中有一项不是 JSON 对象。");
        return false;
    }

    std::optional<std::string> text(const Json::Value& item, const char* field, const std::string& where) {
        const auto& value = item[field];
        if (value.isString() && !value.asString().empty()) {
            return value.asString();
        }
        add(kFieldInvalid, where + " 的 " + field + " 必须是非空文本。");
        return std::nullopt;
    }

    std::optional<int> integer(
        const Json::Value& item,
        const char* field,
        const std::string& where,
        int minimum,
        int maximum) {
        const auto& value = item[field];
        if (value.isInt() && value.asInt() >= minimum && value.asInt() <= maximum) {
            return value.asInt();
        }
        add(kFieldInvalid, where + " 的 " + field + " 必须是 " + std::to_string(minimum) + "～" +
                               std::to_string(maximum) + " 的整数。");
        return std::nullopt;
    }

    // 0～100、至多一位小数的数，转成十分位整数。
    std::optional<int> tenths(const Json::Value& item, const char* field, const std::string& where) {
        const auto& value = item[field];
        if (value.isNumeric()) {
            const auto scaled = value.asDouble() * 10.0;
            const auto rounded = std::llround(scaled);
            if (std::abs(scaled - static_cast<double>(rounded)) < 1e-6 && rounded >= 0 && rounded <= 1000) {
                return static_cast<int>(rounded);
            }
        }
        add(kFieldInvalid, where + " 的 " + field + " 必须是 0～100 之间、至多一位小数的数。");
        return std::nullopt;
    }

    std::optional<bool> flag(const Json::Value& item, const char* field, const std::string& where) {
        if (item[field].isBool()) {
            return item[field].asBool();
        }
        add(kFieldInvalid, where + " 的 " + field + " 必须是 true 或 false。");
        return std::nullopt;
    }

    std::vector<int> pdf_pages(const Json::Value& item, const std::string& where) {
        std::vector<int> pages;
        const auto& values = item["pdf_pages"];
        if (values.isArray()) {
            for (const auto& value : values) {
                if (!value.isInt() || value.asInt() <= 0) {
                    pages.clear();
                    break;
                }
                pages.push_back(value.asInt());
            }
        }
        if (pages.empty()) {
            add(kFieldInvalid, where + " 的 pdf_pages 必须是非空的正整数数组。");
        }
        return pages;
    }

    void parse_parts() {
        const auto* items = definitions("parts.json");
        if (items == nullptr) {
            return;
        }
        for (const auto& item : *items) {
            if (!object(item, "parts.json")) {
                continue;
            }
            const auto id = item["id"].asString();
            const auto where = "部件 " + id;
            const auto name = text(item, "name", where);
            const auto structure =
                parse_layered_structure(item["structure"].isString() ? item["structure"].asString() : "");
            if (!structure.has_value()) {
                add(kFieldInvalid,
                    where + " 的 structure 必须是 deck_system、superstructure、substructure、ancillary 之一。");
            }
            if (name.has_value() && structure.has_value()) {
                parameters_.parts.emplace(id, Jtg5230Part{id, *name, *structure});
            }
        }
    }

    void parse_main_components() {
        const auto* items = definitions("main-components.json");
        if (items == nullptr) {
            return;
        }
        for (const auto& item : *items) {
            if (!object(item, "main-components.json")) {
                continue;
            }
            const auto where = "主要构件定义 " + item["id"].asString();
            const auto family = text(item, "family", where);
            if (!family.has_value()) {
                continue;
            }
            if (parameters_.main_components.contains(*family)) {
                add(kFieldInvalid, where + " 的桥型族 " + *family + " 重复定义。");
                continue;
            }

            Jtg5230MainComponents components;
            const auto& main_ids = item["main_part_ids"];
            if (!main_ids.isArray()) {
                add(kFieldInvalid, where + " 的 main_part_ids 必须是数组。");
            } else {
                for (const auto& part_id : main_ids) {
                    if (!part_id.isString()) {
                        add(kFieldInvalid, where + " 的 main_part_ids 只能是部件 id。");
                        continue;
                    }
                    if (!parameters_.parts.contains(part_id.asString())) {
                        add(kReferenceUnknown, where + " 引用了不存在的部件 " + part_id.asString() + "。");
                    }
                    components.main_part_ids.insert(part_id.asString());
                }
            }

            const auto& mixed = item["mixed_parts"];
            if (!mixed.isArray()) {
                add(kFieldInvalid, where + " 的 mixed_parts 必须是数组。");
            } else {
                for (const auto& entry : mixed) {
                    if (!object(entry, where + " 的 mixed_parts")) {
                        continue;
                    }
                    const auto part_id = text(entry, "part_id", where);
                    const auto members = text(entry, "main_members", where);
                    if (!part_id.has_value() || !members.has_value()) {
                        continue;
                    }
                    if (!parameters_.parts.contains(*part_id)) {
                        add(kReferenceUnknown, where + " 引用了不存在的部件 " + *part_id + "。");
                    }
                    if (components.main_part_ids.contains(*part_id)) {
                        add(kFieldInvalid,
                            where + " 的部件 " + *part_id + " 不能既是主要构件、又是部分为主要构件。");
                    }
                    components.mixed_parts.emplace(*part_id, *members);
                }
            }
            parameters_.main_components.emplace(*family, std::move(components));
        }
    }

    void parse_structures(const Json::Value& item, const std::string& where, Jtg5230ParameterTable& table) {
        const auto& structures = item["structures"];
        bool shape_valid = structures.isArray();
        bool values_valid = true;
        if (shape_valid) {
            for (const auto& entry : structures) {
                const auto structure =
                    entry.isObject() && entry["structure"].isString()
                        ? parse_layered_structure(entry["structure"].asString())
                        : std::nullopt;
                if (!structure.has_value() || !entry["gamma"].isNumeric()) {
                    shape_valid = false;
                    continue;
                }
                const auto scaled = entry["gamma"].asDouble() * 100.0;
                const auto hundredths = static_cast<int>(std::llround(scaled));
                if (std::abs(scaled - hundredths) >= 1e-6 || hundredths <= 0 || hundredths >= 100) {
                    add(kFieldInvalid, where + " 的" + structure_label(*structure) +
                                           "影响系数必须是 0～1 之间的两位小数。");
                    values_valid = false;
                }
                if (!table.gamma_hundredths.emplace(*structure, hundredths).second) {
                    shape_valid = false;
                }
            }
        }
        if (!shape_valid || table.gamma_hundredths.size() != kStructures.size()) {
            add(kFieldInvalid, where + " 必须给出桥面系、上部结构、下部结构、附属构造四个结构的影响系数，各一次。");
            return;
        }
        if (!values_valid) {
            return;
        }
        int total = 0;
        for (const auto& [structure, value] : table.gamma_hundredths) {
            (void)structure;
            total += value;
        }
        if (total != 100) {
            add(kSumInvalid, where + " 四个结构的影响系数合计 " + hundredths_text(total) + "，应为 1。");
        }
    }

    void parse_weights(const Json::Value& weights, const std::string& where, Jtg5230WeightSet& weight_set) {
        if (!weights.isArray() || weights.empty()) {
            add(kFieldInvalid, where + " 没有列出部件权重。");
            return;
        }
        std::set<std::string> part_ids;
        std::set<int> sequence_numbers;
        std::map<LayeredStructure, int> totals;
        bool values_valid = true;
        for (const auto& entry : weights) {
            if (!object(entry, where)) {
                values_valid = false;
                continue;
            }
            const auto part_id = text(entry, "part_id", where);
            const auto name = text(entry, "name", where);
            const auto j = integer(entry, "j", where, 1, 99);
            const auto omega = integer(entry, "omega", where, 1, 100);
            if (!part_id.has_value() || !name.has_value() || !j.has_value() || !omega.has_value()) {
                values_valid = false;
                continue;
            }
            if (!part_ids.insert(*part_id).second) {
                add(kFieldInvalid, where + " 中部件 " + *part_id + " 出现了两次。");
                values_valid = false;
            }
            if (!sequence_numbers.insert(*j).second) {
                add(kFieldInvalid, where + " 中序号 j=" + std::to_string(*j) + " 出现了两次。");
                values_valid = false;
            }
            const auto part = parameters_.parts.find(*part_id);
            if (part == parameters_.parts.end()) {
                add(kReferenceUnknown, where + " 引用了不存在的部件 " + *part_id + "。");
                values_valid = false;
            } else {
                totals[part->second.structure] += *omega;
            }
            weight_set.weights.push_back({*part_id, *name, *j, *omega});
        }
        for (const auto structure : kStructures) {
            const auto total = totals.find(structure);
            if (total == totals.end()) {
                add(kFieldInvalid, where + " 缺少" + structure_label(structure) + "的部件。");
            } else if (values_valid && total->second != 100) {
                add(kSumInvalid, where + structure_label(structure) + "权重合计 " +
                                     std::to_string(total->second) + "，应为 100。");
            }
        }
    }

    void parse_weight_sets(const Json::Value& item, const std::string& where, Jtg5230ParameterTable& table) {
        const auto& weight_sets = item["weight_sets"];
        if (!weight_sets.isArray() || weight_sets.empty()) {
            add(kFieldInvalid, where + " 必须至少有一套权重。");
            return;
        }
        std::set<std::string> set_ids;
        for (const auto& entry : weight_sets) {
            if (!object(entry, where + " 的权重套")) {
                continue;
            }
            const auto id = text(entry, "id", where + " 的权重套");
            if (!id.has_value()) {
                continue;
            }
            const auto set_where = where + "（权重套 " + *id + "）";
            if (!set_ids.insert(*id).second) {
                add(kFieldInvalid, set_where + " 重复。");
                continue;
            }
            Jtg5230WeightSet weight_set;
            weight_set.id = *id;
            weight_set.label = text(entry, "label", set_where).value_or("");
            parse_weights(entry["weights"], set_where, weight_set);
            table.weight_sets.push_back(std::move(weight_set));
        }
    }

    void parse_parameter_tables() {
        const auto* items = definitions("parameter-tables.json");
        if (items == nullptr) {
            return;
        }
        for (const auto& item : *items) {
            if (!object(item, "parameter-tables.json")) {
                continue;
            }
            Jtg5230ParameterTable table;
            table.id = item["id"].asString();
            table.source_table = text(item, "source_table", "参数表 " + table.id).value_or(table.id);
            const auto where = "表 " + table.source_table;
            table.pdf_pages = pdf_pages(item, where);
            parse_structures(item, where, table);
            parse_weight_sets(item, where, table);
            const auto& notes = item["notes"];
            if (notes.isArray()) {
                for (const auto& note : notes) {
                    if (note.isString()) {
                        table.notes.push_back(note.asString());
                    }
                }
            }
            parameters_.parameter_tables.emplace(table.id, std::move(table));
        }
    }

    void parse_evaluation_types() {
        const auto* items = definitions("evaluation-types.json");
        if (items == nullptr) {
            return;
        }
        for (const auto& item : *items) {
            if (!object(item, "evaluation-types.json")) {
                continue;
            }
            Jtg5230EvaluationType type;
            type.id = item["id"].asString();
            const auto where = "评定类型 " + type.id;
            const auto family = text(item, "family", where);
            const auto name = text(item, "name", where);
            const auto table_id = text(item, "parameter_table_id", where);
            if (!family.has_value() || !name.has_value() || !table_id.has_value()) {
                continue;
            }
            if (!parameters_.main_components.contains(*family)) {
                add(kReferenceUnknown, where + " 的桥型族 " + *family + " 没有主要构件定义。");
            }
            if (!parameters_.parameter_tables.contains(*table_id)) {
                add(kReferenceUnknown, where + " 引用了不存在的参数表 " + *table_id + "。");
            }
            type.family = *family;
            type.name = *name;
            type.parameter_table_id = *table_id;
            parameters_.evaluation_types.emplace(type.id, std::move(type));
        }
    }

    void parse_component_grades(const Json::Value& definition) {
        const std::string where = "表 3.2.6";
        const auto& grades = definition["grades"];
        if (!grades.isArray()) {
            add(kFieldInvalid, where + " 的 grades 必须是数组。");
            return;
        }
        std::array<int, 6> hits{};
        for (const auto& entry : grades) {
            if (!object(entry, where)) {
                continue;
            }
            const auto grade = integer(entry, "grade", where, 1, 5);
            const auto state = text(entry, "state", where);
            const auto& scales = entry["eci"];
            if (!scales.isArray()) {
                add(kFieldInvalid, where + " 的 eci 必须是数组。");
                continue;
            }
            if (!grade.has_value() || !state.has_value()) {
                continue;
            }
            for (const auto& scale : scales) {
                if (!scale.isInt() || scale.asInt() < 0 || scale.asInt() > 5) {
                    add(kFieldInvalid, where + " 的 ECI 必须是 0～5 的整数。");
                    continue;
                }
                const auto eci = static_cast<std::size_t>(scale.asInt());
                ++hits[eci];
                parameters_.component_grades[eci] = {*grade, *state};
            }
        }
        for (std::size_t eci = 0; eci < hits.size(); ++eci) {
            if (hits[eci] != 1) {
                add(kCoverageInvalid, where + " 中 ECI " + std::to_string(eci) +
                                          (hits[eci] == 0 ? " 没有对应的等级。" : " 出现了不止一次。"));
            }
        }
    }

    void parse_span_grades(const Json::Value& definition) {
        const std::string where = "表 3.3.2";
        const auto& grades = definition["grades"];
        if (!grades.isArray()) {
            add(kFieldInvalid, where + " 的 grades 必须是数组。");
            return;
        }
        bool values_valid = true;
        for (const auto& entry : grades) {
            if (!object(entry, where)) {
                values_valid = false;
                continue;
            }
            const auto grade = integer(entry, "grade", where, 1, 5);
            const auto lower = tenths(entry, "lower", where);
            const auto upper = tenths(entry, "upper", where);
            const auto lower_inclusive = flag(entry, "lower_inclusive", where);
            const auto upper_inclusive = flag(entry, "upper_inclusive", where);
            if (!grade || !lower || !upper || !lower_inclusive || !upper_inclusive) {
                values_valid = false;
                continue;
            }
            parameters_.span_grades.push_back({*grade, *lower, *lower_inclusive, *upper, *upper_inclusive});
        }
        if (!values_valid) {
            return;
        }
        // 指数总是一位小数，逐个十分位检查：每个值必须恰好落在一档里。
        for (int index_tenths = 0; index_tenths <= 1000; ++index_tenths) {
            int matches = 0;
            for (const auto& band : parameters_.span_grades) {
                matches += band_contains(band, index_tenths) ? 1 : 0;
            }
            if (matches != 1) {
                add(kCoverageInvalid, where + " 的分档在 " + tenths_text(index_tenths) +
                                          (matches == 0 ? " 处有缺口。" : " 处重叠。"));
                return;
            }
        }
    }

    void parse_index_limits(const Json::Value& definition) {
        const std::string where = "3.4.3 指数上限";
        const auto& limits = definition["limits"];
        if (!limits.isArray()) {
            add(kFieldInvalid, where + " 的 limits 必须是数组。");
            return;
        }
        for (const auto& entry : limits) {
            if (!object(entry, where)) {
                continue;
            }
            const auto grade = integer(entry, "grade", where, 2, 5);
            const auto value = tenths(entry, "value", where);
            if (!grade || !value) {
                continue;
            }
            if (!parameters_.index_limit_tenths.emplace(*grade, *value).second) {
                add(kFieldInvalid, where + " 中 " + std::to_string(*grade) + " 类出现了两次。");
            }
        }
        for (int grade = 2; grade <= 5; ++grade) {
            if (!parameters_.index_limit_tenths.contains(grade)) {
                add(kCoverageInvalid, where + " 缺少 " + std::to_string(grade) + " 类的上限。");
            }
        }
    }

    void parse_classification() {
        const auto* items = definitions("classification.json");
        if (const auto* definition = required_definition(items, kComponentClassificationId)) {
            parse_component_grades(*definition);
        }
        if (const auto* definition = required_definition(items, kSpanClassificationId)) {
            parse_span_grades(*definition);
        }
        if (const auto* definition = required_definition(items, kIndexLimitId)) {
            parse_index_limits(*definition);
        }
    }

    void parse_controls() {
        const auto* items = definitions("single-item-controls.json");
        if (items == nullptr) {
            return;
        }
        for (const auto& item : *items) {
            if (!object(item, "single-item-controls.json")) {
                continue;
            }
            Jtg5230Control control;
            control.id = item["id"].asString();
            const auto where = "单项控制 " + control.id;
            const auto clause = text(item, "source_clause", where);
            const auto label = text(item, "label", where);
            const auto result_grade = integer(item, "result_grade", where, 5, 5);
            if (!clause.has_value() || !label.has_value() || !result_grade.has_value()) {
                continue;
            }
            control.source_clause = *clause;
            control.label = *label;
            control.result_grade = *result_grade;
            parameters_.controls.emplace(control.id, std::move(control));
        }
    }

    void parse_maintenance() {
        const auto* items = definitions("maintenance-strategies.json");
        const auto* definition = required_definition(items, kMaintenanceId);
        if (definition == nullptr) {
            return;
        }
        const std::string where = "表 3.5.2";
        const auto& strategies = (*definition)["strategies"];
        if (!strategies.isArray()) {
            add(kFieldInvalid, where + " 的 strategies 必须是数组。");
            return;
        }
        for (const auto& entry : strategies) {
            if (!object(entry, where)) {
                continue;
            }
            const auto grade = integer(entry, "grade", where, 1, 5);
            const auto& values = entry["items"];
            std::vector<std::string> names;
            if (values.isArray()) {
                for (const auto& value : values) {
                    if (value.isString() && !value.asString().empty()) {
                        names.push_back(value.asString());
                    }
                }
            }
            if (names.empty() || names.size() != values.size()) {
                add(kFieldInvalid, where + " 的 items 必须是非空文本数组。");
                continue;
            }
            if (!grade.has_value()) {
                continue;
            }
            if (!parameters_.maintenance_strategies.emplace(*grade, std::move(names)).second) {
                add(kFieldInvalid, where + " 中 " + std::to_string(*grade) + " 类出现了两次。");
            }
        }
        for (int grade = 1; grade <= 5; ++grade) {
            if (!parameters_.maintenance_strategies.contains(grade)) {
                add(kCoverageInvalid, where + " 缺少 " + std::to_string(grade) + " 类的养护对策。");
            }
        }
    }

    const StandardPackage& package_;
    Jtg5230Parameters parameters_;
    std::vector<StandardIssue> issues_;
};

}  // namespace

std::string to_string(LayeredStructure structure) {
    switch (structure) {
        case LayeredStructure::deck_system:
            return "deck_system";
        case LayeredStructure::superstructure:
            return "superstructure";
        case LayeredStructure::substructure:
            return "substructure";
        case LayeredStructure::ancillary:
            return "ancillary";
    }
    return {};
}

std::optional<LayeredStructure> parse_layered_structure(std::string_view value) {
    for (const auto structure : kStructures) {
        if (to_string(structure) == value) {
            return structure;
        }
    }
    return std::nullopt;
}

const Jtg5230WeightSet* Jtg5230ParameterTable::find_weight_set(const std::string& id) const {
    for (const auto& weight_set : weight_sets) {
        if (weight_set.id == id) {
            return &weight_set;
        }
    }
    return nullptr;
}

MainComponentDesignation Jtg5230Parameters::designation(
    const std::string& family,
    const std::string& part_id) const {
    const auto found = main_components.find(family);
    if (found == main_components.end()) {
        return MainComponentDesignation::not_main;
    }
    if (found->second.main_part_ids.contains(part_id)) {
        return MainComponentDesignation::main;
    }
    if (found->second.mixed_parts.contains(part_id)) {
        return MainComponentDesignation::mixed;
    }
    return MainComponentDesignation::not_main;
}

int Jtg5230Parameters::classify_span(int index_tenths) const {
    for (const auto& band : span_grades) {
        if (band_contains(band, index_tenths)) {
            return band.grade;
        }
    }
    return 0;  // 解析时已保证 0～1000 每个十分位都有一档，走不到这里
}

Jtg5230ParametersResult parse_jtg5230_parameters(const StandardPackage& package) {
    return Parser(package).run();
}

}  // namespace bridge_report::standards
