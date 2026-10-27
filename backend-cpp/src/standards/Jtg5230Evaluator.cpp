#include "bridge_report/standards/Jtg5230Evaluator.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace bridge_report::standards {

namespace {

constexpr const char* kAlgorithmId = "jtg-t-5230-2026";

AssessmentIssue issue(std::string code, std::string message, std::string entity_id) {
    return {std::move(code), std::move(message), std::move(entity_id), {}};
}

bool weight_set_contains(const Jtg5230WeightSet& weight_set, const std::string& part_id) {
    return std::any_of(weight_set.weights.begin(), weight_set.weights.end(), [&](const Jtg5230Weight& weight) {
        return weight.part_id == part_id;
    });
}

// 评定单元的查表结果；类型或权重套不存在时对应指针为空，由 validate 报出。
struct UnitView {
    const LayeredUnitInput* input{nullptr};
    const Jtg5230EvaluationType* type{nullptr};
    const Jtg5230ParameterTable* table{nullptr};
    const Jtg5230WeightSet* weight_set{nullptr};
    std::set<std::string> not_required;
};

struct InputIndex {
    std::vector<UnitView> units;                    // 按输入顺序
    std::map<std::string, std::size_t> span_unit;   // 桥跨 → 所在单元（重复的桥跨取第一次出现）
};

InputIndex index_input(const LayeredAssessmentInput& input, const Jtg5230Parameters& parameters) {
    InputIndex index;
    for (const auto& unit_input : input.units) {
        UnitView unit;
        unit.input = &unit_input;
        const auto type = parameters.evaluation_types.find(unit_input.evaluation_type_id);
        if (type != parameters.evaluation_types.end()) {
            unit.type = &type->second;
            unit.table = &parameters.parameter_tables.at(type->second.parameter_table_id);
            unit.weight_set = unit.table->find_weight_set(unit_input.weight_set_id);
        }
        unit.not_required.insert(
            unit_input.not_required_part_ids.begin(), unit_input.not_required_part_ids.end());
        for (const auto& span_id : unit_input.span_ids) {
            index.span_unit.emplace(span_id, index.units.size());
        }
        index.units.push_back(std::move(unit));
    }
    return index;
}

void check_scale(
    const std::optional<int>& value,
    const char* name,
    int minimum,
    int maximum,
    const std::string& component_id,
    std::vector<AssessmentIssue>& issues) {
    if (value.has_value() && (*value < minimum || *value > maximum)) {
        issues.push_back(issue(
            "scale_out_of_range",
            "构件 " + component_id + " 的 " + name + " 为 " + std::to_string(*value) + "，应在 " +
                std::to_string(minimum) + "～" + std::to_string(maximum) + " 之间。",
            component_id));
    }
}

}  // namespace

Jtg5230Evaluator::Jtg5230Evaluator(StandardManifest manifest, Jtg5230Parameters parameters)
    : manifest_(std::move(manifest)), parameters_(std::move(parameters)) {}

Jtg5230EvaluatorCreation Jtg5230Evaluator::create(const StandardPackage& package) {
    Jtg5230EvaluatorCreation creation;
    if (package.manifest.algorithm_id != kAlgorithmId) {
        creation.issues.push_back({
            "algorithm_unsupported",
            "规则包声明的算法不是 jtg-t-5230-2026，不能用 5230 评分器。",
        });
        return creation;
    }
    auto parsed = parse_jtg5230_parameters(package);
    if (!parsed.ok()) {
        creation.issues = std::move(parsed.issues);
        return creation;
    }
    creation.evaluator.reset(new Jtg5230Evaluator(package.manifest, std::move(*parsed.parameters)));
    return creation;
}

std::string Jtg5230Evaluator::algorithm_id() const {
    return manifest_.algorithm_id;
}

const StandardManifest& Jtg5230Evaluator::metadata() const noexcept {
    return manifest_;
}

std::vector<AssessmentIssue> Jtg5230Evaluator::validate(const LayeredAssessmentInput& input) const {
    std::vector<AssessmentIssue> issues;
    if (input.units.empty()) {
        issues.push_back(issue("units_missing", "评定输入没有评定单元。", {}));
    }

    const auto index = index_input(input, parameters_);
    std::set<std::string> unit_ids;
    std::set<std::string> span_ids;
    for (const auto& unit : index.units) {
        const auto& unit_input = *unit.input;
        const auto& unit_id = unit_input.unit_id;
        if (unit_id.empty() || !unit_ids.insert(unit_id).second) {
            issues.push_back(issue("unit_id_invalid", "评定单元 id 为空或重复：" + unit_id + "。", unit_id));
        }
        if (unit.type == nullptr) {
            issues.push_back(issue(
                "evaluation_type_unknown",
                "评定单元 " + unit_id + " 的评定类型不存在：" + unit_input.evaluation_type_id + "。",
                unit_id));
        } else if (unit.weight_set == nullptr) {
            issues.push_back(issue(
                "weight_set_unknown",
                "评定单元 " + unit_id + " 选用的权重套 " + unit_input.weight_set_id + " 不在表 " +
                    unit.table->source_table + " 中。",
                unit_id));
        }
        if (unit_input.span_ids.empty()) {
            issues.push_back(issue("unit_spans_missing", "评定单元 " + unit_id + " 没有桥跨。", unit_id));
        }
        for (const auto& span_id : unit_input.span_ids) {
            if (span_id.empty() || !span_ids.insert(span_id).second) {
                issues.push_back(issue("span_id_invalid", "桥跨 id 为空或在全桥重复：" + span_id + "。", span_id));
            }
        }
        if (unit.weight_set == nullptr) {
            continue;
        }

        std::set<std::string> listed;
        for (const auto& part_id : unit_input.not_required_part_ids) {
            if (!weight_set_contains(*unit.weight_set, part_id) || !listed.insert(part_id).second) {
                issues.push_back(issue(
                    "not_required_part_invalid",
                    "评定单元 " + unit_id + " 列为无须设置的部件 " + part_id + " 不在所选权重套里，或重复列出。",
                    part_id));
                continue;
            }
            for (const auto& other : unit.table->weight_sets) {
                if (&other != unit.weight_set && !weight_set_contains(other, part_id)) {
                    issues.push_back(issue(
                        "explicit_weight_set_available",
                        "表 " + unit.table->source_table + " 已给出不含" + parameters_.parts.at(part_id).name +
                            "的权重套 " + other.id + "（" + other.label + "），请改选这套权重，不要按比例重分。",
                        part_id));
                    break;
                }
            }
        }
        std::map<LayeredStructure, std::pair<int, int>> counts;  // 结构 → (部件数, 其中无须设置的数)
        for (const auto& weight : unit.weight_set->weights) {
            auto& count = counts[parameters_.parts.at(weight.part_id).structure];
            ++count.first;
            count.second += listed.contains(weight.part_id) ? 1 : 0;
        }
        for (const auto& [structure, count] : counts) {
            if (count.first == count.second) {
                issues.push_back(issue(
                    "structure_without_parts",
                    "评定单元 " + unit_id + " 把" + display_name(structure) + "的部件全部列为无须设置，无法按比例重分权重。",
                    unit_id));
            }
        }
    }

    std::set<std::string> component_ids;
    for (const auto& component : input.components) {
        const auto& component_id = component.component_id;
        if (component_id.empty() || !component_ids.insert(component_id).second) {
            issues.push_back(issue("component_id_invalid", "构件 id 为空或重复：" + component_id + "。", component_id));
        }
        check_scale(component.eddi, "EDDI", 0, 2, component_id, issues);
        check_scale(component.esdi, "ESDI", 0, 3, component_id, issues);
        check_scale(component.efi, "EFI", 0, 2, component_id, issues);
        check_scale(component.eai, "EAI", -1, 1, component_id, issues);
        if (component.span_ids.empty()) {
            issues.push_back(issue("component_spans_missing", "构件 " + component_id + " 没有所属桥跨。", component_id));
            continue;
        }

        std::vector<const UnitView*> touched;
        bool spans_known = true;
        for (const auto& span_id : component.span_ids) {
            const auto found = index.span_unit.find(span_id);
            if (found == index.span_unit.end()) {
                issues.push_back(issue(
                    "component_span_unknown", "构件 " + component_id + " 的桥跨 " + span_id + " 不存在。", component_id));
                spans_known = false;
                continue;
            }
            const auto* unit = &index.units[found->second];
            if (std::find(touched.begin(), touched.end(), unit) == touched.end()) {
                touched.push_back(unit);
            }
        }
        // 桥跨或单元本身有问题时上面已报过，不再连带报部件归属。
        if (!spans_known || std::any_of(touched.begin(), touched.end(), [](const UnitView* unit) {
                return unit->weight_set == nullptr;
            })) {
            continue;
        }
        std::set<std::string> families;
        for (const auto* unit : touched) {
            families.insert(unit->type->family);
        }
        if (families.size() > 1) {
            issues.push_back(issue(
                "component_spans_cross_families", "构件 " + component_id + " 所在的评定单元分属不同桥型族。", component_id));
            continue;
        }

        bool part_valid = true;
        for (const auto* unit : touched) {
            if (!weight_set_contains(*unit->weight_set, component.part_id)) {
                issues.push_back(issue(
                    "component_part_not_in_unit",
                    "构件 " + component_id + " 的部件 " + component.part_id + " 不在评定单元 " +
                        unit->input->unit_id + " 的权重套里。",
                    component_id));
                part_valid = false;
            } else if (unit->not_required.contains(component.part_id)) {
                issues.push_back(issue(
                    "component_part_not_required",
                    "构件 " + component_id + " 的部件 " + component.part_id + " 在评定单元 " +
                        unit->input->unit_id + " 中已列为无须设置。",
                    component_id));
                part_valid = false;
            }
        }
        if (!part_valid) {
            continue;
        }

        const auto& family = *families.begin();
        const auto designation = parameters_.designation(family, component.part_id);
        if (designation == MainComponentDesignation::mixed && !component.is_main.has_value()) {
            issues.push_back(issue(
                "main_component_flag_required",
                "构件 " + component_id + " 的部件“" + parameters_.parts.at(component.part_id).name +
                    "”只有部分构件是主要构件（" +
                    parameters_.main_components.at(family).mixed_parts.at(component.part_id) +
                    "），须标明它是不是主要构件。",
                component_id));
        } else if (designation != MainComponentDesignation::mixed && component.is_main.has_value()) {
            issues.push_back(issue(
                "main_component_flag_not_applicable",
                "构件 " + component_id + " 的部件按表 3.3.3 已能确定是否主要构件，不接受输入。",
                component_id));
        }
    }

    for (const auto& control_id : input.triggered_control_ids) {
        if (!parameters_.controls.contains(control_id)) {
            issues.push_back(issue("single_item_control_unknown", "单项控制指标不存在：" + control_id + "。", control_id));
        }
    }
    return issues;
}

LayeredAssessmentOutcome Jtg5230Evaluator::evaluate(const LayeredAssessmentInput& input) const {
    LayeredAssessmentOutcome outcome;
    outcome.issues = validate(input);
    if (!outcome.issues.empty()) {
        return outcome;
    }
    LayeredAssessmentResult result;
    result.standard_id = manifest_.standard_id;
    result.package_version = manifest_.package_version;
    outcome.result = std::move(result);
    return outcome;
}

std::string Jtg5230Evaluator::explain(const LayeredAssessmentResult& result) const {
    (void)result;
    return {};
}

}  // namespace bridge_report::standards
