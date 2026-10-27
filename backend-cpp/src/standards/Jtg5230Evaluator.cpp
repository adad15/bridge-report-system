#include "bridge_report/standards/Jtg5230Evaluator.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
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

// 构件层（3.2）：主要构件判定 → EDI → ECI → ECL、状态词、养护对策。
LayeredComponentResult evaluate_component(
    const LayeredComponentInput& component,
    const UnitView& unit,
    const Jtg5230Parameters& parameters,
    std::vector<AssessmentTraceEntry>& trace) {
    LayeredComponentResult result;
    result.component_id = component.component_id;
    result.part_id = component.part_id;
    const auto designation = parameters.designation(unit.type->family, component.part_id);
    result.is_main = designation == MainComponentDesignation::main ||
                     (designation == MainComponentDesignation::mixed && component.is_main.value_or(false));
    result.edi = component.missing ? 3 : (std::max)(component.eddi.value_or(0), component.esdi.value_or(0));
    const int efi = component.efi.value_or(0);
    // 影响状况只评主要构件（第 15 章）；下限 0 见设计第 7 节第 1 条。
    const int eai = result.is_main ? component.eai.value_or(0) : 0;
    result.eai_ignored = !result.is_main && component.eai.has_value();
    result.eci = std::clamp(result.edi + efi + eai, 0, 5);
    const auto& grade = parameters.component_grades[static_cast<std::size_t>(result.eci)];
    result.ecl = grade.grade;
    result.state = grade.state;
    result.maintenance_strategies = parameters.maintenance_strategies.at(result.ecl);

    AssessmentTraceEntry entry;
    entry.step = "component";
    entry.rule_id = "jtg5230.classification.component";
    entry.entity_id = result.component_id;
    entry.source_reference = "3.2.5、表 3.2.6";
    entry.inputs["edi"] = result.edi;
    entry.inputs["efi"] = efi;
    entry.inputs["eai"] = eai;
    entry.inputs["missing"] = component.missing;
    entry.output["eci"] = result.eci;
    entry.output["ecl"] = result.ecl;
    trace.push_back(std::move(entry));
    return result;
}

// 非负有理数，分母 > 0。桥跨和全桥指数都按十进制保留一位小数，用分数算才不会被二进制
// 浮点误差带偏（99.65 用 double 算是 99.6499…）。
struct Fraction {
    std::int64_t numerator{0};
    std::int64_t denominator{1};
};

Fraction add(Fraction left, Fraction right) {
    Fraction sum{
        left.numerator * right.denominator + right.numerator * left.denominator,
        left.denominator * right.denominator,
    };
    const auto divisor = std::gcd(sum.numerator, sum.denominator);
    if (divisor > 1) {
        sum.numerator /= divisor;
        sum.denominator /= divisor;
    }
    return sum;
}

// 四舍五入到整数：floor((2n + d) / (2d))，全程整数运算。
int round_half_up(Fraction value) {
    return static_cast<int>((2 * value.numerator + value.denominator) / (2 * value.denominator));
}

// 单元所选权重套里参与计算的一行。B.1.3：全桥无须设置的部件不参与，其权重按比例分给同结构
// 其他部件，即有效 ω = ω × 100 / 该结构剩余部件 ω 之和（structure_total）。
struct EffectiveWeight {
    const Jtg5230Weight* weight{nullptr};
    LayeredStructure structure{LayeredStructure::superstructure};
    int structure_total{100};
    int gamma_hundredths{0};
};

std::vector<EffectiveWeight> effective_weights(const UnitView& unit, const Jtg5230Parameters& parameters) {
    std::map<LayeredStructure, int> totals;
    for (const auto& weight : unit.weight_set->weights) {
        if (!unit.not_required.contains(weight.part_id)) {
            totals[parameters.parts.at(weight.part_id).structure] += weight.omega;
        }
    }
    std::vector<EffectiveWeight> weights;
    for (const auto& weight : unit.weight_set->weights) {
        if (unit.not_required.contains(weight.part_id)) {
            continue;
        }
        const auto structure = parameters.parts.at(weight.part_id).structure;
        weights.push_back({&weight, structure, totals.at(structure), unit.table->gamma_hundredths.at(structure)});
    }
    return weights;
}

struct SpanEvaluation {
    LayeredSpanResult result;
    int index_tenths{0};  // 改判后的指数×10，单元合成用
};

// 桥跨层（3.3）：式 3.3.1 → 表 3.3.2 → 3.3.3 主要构件改判。
SpanEvaluation evaluate_span(
    const std::string& span_id,
    const UnitView& unit,
    const std::vector<EffectiveWeight>& weights,
    const std::vector<LayeredComponentInput>& components,
    const std::vector<LayeredComponentResult>& component_results,
    const Jtg5230Parameters& parameters,
    std::vector<AssessmentTraceEntry>& trace) {
    const auto in_span = [&](std::size_t index) {
        const auto& span_ids = components[index].span_ids;
        return std::find(span_ids.begin(), span_ids.end(), span_id) != span_ids.end();
    };

    SpanEvaluation evaluation;
    auto& span = evaluation.result;
    span.span_id = span_id;
    AssessmentTraceEntry entry;
    entry.step = "span";
    entry.rule_id = unit.table->id;
    entry.entity_id = span_id;
    entry.source_reference = "3.3.1、表 3.3.2";
    entry.inputs["unit_id"] = unit.input->unit_id;
    entry.inputs["parts"] = Json::Value(Json::arrayValue);

    Fraction deduction;  // 单位：十分位
    for (const auto& item : weights) {
        LayeredPartResult part;
        part.part_id = item.weight->part_id;
        part.table_name = item.weight->table_name;
        part.structure = to_string(item.structure);
        for (std::size_t index = 0; index < components.size(); ++index) {
            if (components[index].part_id != part.part_id || !in_span(index)) {
                continue;
            }
            if (part.worst_component_id.empty() || component_results[index].eci > part.ci) {
                part.ci = component_results[index].eci;
                part.worst_component_id = components[index].component_id;
            }
        }
        // CI·ω·γ/5 换成十分位：2 × γ百分数 × CI × ω / 结构剩余 ω 之和。
        deduction = add(
            deduction,
            {2LL * item.gamma_hundredths * part.ci * item.weight->omega, item.structure_total});
        part.effective_omega = item.weight->omega * 100.0 / item.structure_total;
        part.gamma = item.gamma_hundredths / 100.0;
        part.contribution = part.ci * part.effective_omega * part.gamma / 5.0;

        Json::Value part_trace;
        part_trace["part_id"] = part.part_id;
        part_trace["ci"] = part.ci;
        part_trace["effective_omega"] = part.effective_omega;
        part_trace["gamma"] = part.gamma;
        entry.inputs["parts"].append(std::move(part_trace));
        span.parts.push_back(std::move(part));
    }

    evaluation.index_tenths =
        round_half_up({1000 * deduction.denominator - deduction.numerator, deduction.denominator});
    span.calculated_index = evaluation.index_tenths / 10.0;
    span.calculated_grade = parameters.classify_span(evaluation.index_tenths);
    span.index = span.calculated_index;
    span.grade = span.calculated_grade;
    entry.output["index"] = span.calculated_index;
    entry.output["grade"] = span.calculated_grade;
    trace.push_back(std::move(entry));

    // 主要构件 4、5 类且影响桥梁安全，并且比算出的等级更差时才改判；指数取该等级上限。
    std::optional<std::size_t> governing;
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto& candidate = component_results[index];
        if (!in_span(index) || !candidate.is_main || candidate.ecl < 4 ||
            !components[index].affects_bridge_safety) {
            continue;
        }
        if (!governing.has_value() || candidate.ecl > component_results[*governing].ecl) {
            governing = index;
        }
    }
    if (governing.has_value() && component_results[*governing].ecl > span.calculated_grade) {
        const auto& component = component_results[*governing];
        span.grade = component.ecl;
        evaluation.index_tenths = parameters.index_limit_tenths.at(component.ecl);
        span.index = evaluation.index_tenths / 10.0;
        span.override_component_id = component.component_id;

        AssessmentTraceEntry override_entry;
        override_entry.step = "main_component_override";
        override_entry.rule_id = "jtg5230.classification.index_limit";
        override_entry.entity_id = span_id;
        override_entry.source_reference = "3.3.3";
        override_entry.inputs["component_id"] = component.component_id;
        override_entry.inputs["ecl"] = component.ecl;
        override_entry.inputs["calculated_grade"] = span.calculated_grade;
        override_entry.output["grade"] = span.grade;
        override_entry.output["index"] = span.index;
        trace.push_back(std::move(override_entry));
    }
    return evaluation;
}

struct UnitAggregate {
    int index_tenths{0};
    int grade{0};
    std::string rule_clause;
};

// 评定单元（3.4.1～3.4.3）。指数一律用已保留一位小数的桥跨指数（十分位整数）代入，
// 见设计第 7 节第 3 条。
UnitAggregate aggregate_unit(
    const std::vector<int>& index_tenths,
    const std::vector<int>& grades,
    const Jtg5230Parameters& parameters) {
    if (index_tenths.size() == 1) {
        return {index_tenths.front(), grades.front(), "3.4.1"};
    }
    const auto count = static_cast<std::int64_t>(index_tenths.size());
    const auto sum = std::accumulate(index_tenths.begin(), index_tenths.end(), std::int64_t{0});
    const auto worst = *std::max_element(grades.begin(), grades.end());
    if (worst == *std::min_element(grades.begin(), grades.end())) {
        return {round_half_up({sum, count}), worst, "3.4.2"};
    }
    // 式 3.4.3：BCI = min + (lim − min)·(平均 − min)/(max − min)。等级不一致时 max > min 必然成立：
    // 各档区间不相交，改判后的指数也落在本档区间内。
    const std::int64_t minimum = *std::min_element(index_tenths.begin(), index_tenths.end());
    const std::int64_t maximum = *std::max_element(index_tenths.begin(), index_tenths.end());
    const std::int64_t limit = parameters.index_limit_tenths.at(worst);
    const Fraction offset{(limit - minimum) * (sum - count * minimum), count * (maximum - minimum)};
    return {static_cast<int>(minimum) + round_half_up(offset), worst, "3.4.3"};
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

    // 校验已通过：类型、权重套、桥跨都存在，构件所在单元同属一个桥型族。
    const auto index = index_input(input, parameters_);
    for (const auto& component : input.components) {
        const auto& unit = index.units[index.span_unit.at(component.span_ids.front())];
        result.components.push_back(evaluate_component(component, unit, parameters_, result.trace));
    }

    std::vector<int> unit_tenths;  // 各单元指数×10，与 result.units 对应
    for (const auto& unit : index.units) {
        LayeredUnitResult unit_result;
        unit_result.unit_id = unit.input->unit_id;
        unit_result.evaluation_type_id = unit.input->evaluation_type_id;
        unit_result.weight_set_id = unit.input->weight_set_id;
        const auto weights = effective_weights(unit, parameters_);
        std::vector<int> span_tenths;
        std::vector<int> span_grades;
        for (const auto& span_id : unit.input->span_ids) {
            auto span = evaluate_span(
                span_id, unit, weights, input.components, result.components, parameters_, result.trace);
            span_tenths.push_back(span.index_tenths);
            span_grades.push_back(span.result.grade);
            unit_result.spans.push_back(std::move(span.result));
        }

        const auto aggregate = aggregate_unit(span_tenths, span_grades, parameters_);
        unit_result.index = aggregate.index_tenths / 10.0;
        unit_result.grade = aggregate.grade;
        unit_result.rule_clause = aggregate.rule_clause;
        unit_tenths.push_back(aggregate.index_tenths);

        AssessmentTraceEntry entry;
        entry.step = "unit";
        entry.rule_id = "jtg5230.classification.index_limit";
        entry.entity_id = unit_result.unit_id;
        entry.source_reference = unit_result.rule_clause;
        entry.inputs["spans"] = Json::Value(Json::arrayValue);
        for (const auto& span : unit_result.spans) {
            Json::Value span_trace;
            span_trace["span_id"] = span.span_id;
            span_trace["index"] = span.index;
            span_trace["grade"] = span.grade;
            entry.inputs["spans"].append(std::move(span_trace));
        }
        entry.output["index"] = unit_result.index;
        entry.output["grade"] = unit_result.grade;
        result.trace.push_back(std::move(entry));
        result.units.push_back(std::move(unit_result));
    }

    // 3.4.4：取最差单元——等级最差，同等级取指数较低者，再相同取输入顺序在前的。
    std::size_t governing = 0;
    for (std::size_t unit_index = 1; unit_index < result.units.size(); ++unit_index) {
        const auto& candidate = result.units[unit_index];
        const auto& current = result.units[governing];
        if (candidate.grade > current.grade ||
            (candidate.grade == current.grade && unit_tenths[unit_index] < unit_tenths[governing])) {
            governing = unit_index;
        }
    }
    result.governing_unit_id = result.units[governing].unit_id;
    result.layered_index = result.units[governing].index;
    result.layered_grade = result.units[governing].grade;
    result.final_grade = result.layered_grade;

    AssessmentTraceEntry bridge_entry;
    bridge_entry.step = "bridge";
    bridge_entry.entity_id = result.governing_unit_id;
    bridge_entry.source_reference = "3.4.4";
    bridge_entry.inputs["units"] = Json::Value(Json::arrayValue);
    for (const auto& unit : result.units) {
        Json::Value unit_trace;
        unit_trace["unit_id"] = unit.unit_id;
        unit_trace["index"] = unit.index;
        unit_trace["grade"] = unit.grade;
        bridge_entry.inputs["units"].append(std::move(unit_trace));
    }
    bridge_entry.output["index"] = result.layered_index;
    bridge_entry.output["grade"] = result.layered_grade;
    result.trace.push_back(std::move(bridge_entry));

    // 3.4.5：出现任一单项控制指标，全桥评为 5 类；分层评定结果原样保留，重复的 id 只计一次。
    std::set<std::string> seen_controls;
    for (const auto& control_id : input.triggered_control_ids) {
        if (!seen_controls.insert(control_id).second) {
            continue;
        }
        const auto& control = parameters_.controls.at(control_id);
        result.triggered_controls.push_back({control.id, control.source_clause, control.label});
        result.final_grade = (std::max)(result.final_grade, control.result_grade);

        AssessmentTraceEntry control_entry;
        control_entry.step = "single_item_control";
        control_entry.rule_id = control.id;
        control_entry.entity_id = control.id;
        control_entry.source_reference = control.source_clause;
        control_entry.output["final_grade"] = result.final_grade;
        result.trace.push_back(std::move(control_entry));
    }

    result.explanation = explain(result);
    outcome.result = std::move(result);
    return outcome;
}

std::string Jtg5230Evaluator::explain(const LayeredAssessmentResult& result) const {
    std::ostringstream text;
    text << std::fixed << std::setprecision(1);
    text << "依据 " << manifest_.standard_code << "（规则包 " << manifest_.package_version
         << "）分层综合评定：全桥技术状况指数 " << result.layered_index << "，" << result.layered_grade << " 类";
    for (const auto& unit : result.units) {
        if (unit.unit_id == result.governing_unit_id) {
            text << "（" << unit.unit_id << " 单元，按 " << unit.rule_clause << "）";
            break;
        }
    }
    text << "。";
    if (!result.triggered_controls.empty()) {
        text << "出现单项控制指标 " << result.triggered_controls.size() << " 项（";
        for (std::size_t index = 0; index < result.triggered_controls.size(); ++index) {
            text << (index == 0 ? "" : "、") << result.triggered_controls[index].label;
        }
        text << "），全桥技术状况等级评为 " << result.final_grade << " 类。";
    }
    return text.str();
}

}  // namespace bridge_report::standards
