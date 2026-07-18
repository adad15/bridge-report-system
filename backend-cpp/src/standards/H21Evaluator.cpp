#include "bridge_report/standards/H21Evaluator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace bridge_report::standards {

namespace {

constexpr std::string_view kComponentScoreRule = "h21.calculation.component_score";
constexpr std::string_view kCategoryScoreRule = "h21.calculation.component_category_score";
constexpr std::string_view kCountFactorRule = "h21.calculation.component_count_factor";
constexpr std::string_view kPartScoreRule = "h21.calculation.structure_part_score";
constexpr std::string_view kOverallScoreRule = "h21.calculation.overall_score";
constexpr std::string_view kGradeRule = "h21.grade_boundaries.standard";
constexpr std::string_view kStructureWeightRule = "h21.weight_set.structure.default";
constexpr std::string_view kWorstMajorControl = "h21.control.worst_major_component";
constexpr std::string_view kGrade3CombinationControl =
    "h21.control.overall_grade_3_combination";

AssessmentIssue issue(
    std::string code,
    std::string message,
    std::string entity_id = {},
    std::string rule_id = {}) {
    return {
        std::move(code),
        std::move(message),
        std::move(entity_id),
        std::move(rule_id),
    };
}

const StandardDefinition* definition(const StandardPackage& package, std::string_view id) {
    const auto found = package.definitions.find(std::string(id));
    return found == package.definitions.end() ? nullptr : &found->second;
}

bool string_array_contains(const Json::Value& values, const std::string& expected) {
    if (!values.isArray()) {
        return false;
    }
    return std::any_of(values.begin(), values.end(), [&](const Json::Value& value) {
        return value.isString() && value.asString() == expected;
    });
}

std::string source_reference(const StandardDefinition& rule) {
    static constexpr std::array fields = {
        "source_clause",
        "source_table",
    };
    for (const auto* field : fields) {
        if (rule.payload[field].isString() && !rule.payload[field].asString().empty()) {
            return rule.payload[field].asString();
        }
    }
    const auto& tables = rule.payload["source_tables"];
    if (tables.isArray() && !tables.empty() && tables[0].isString()) {
        return tables[0].asString();
    }
    return rule.id;
}

struct DefectRuleView {
    const Json::Value* indicator{nullptr};
    const Json::Value* applicable_component_ids{nullptr};
};

std::map<std::string, DefectRuleView> defect_rules(const StandardPackage& package) {
    std::map<std::string, DefectRuleView> result;
    const auto document = package.documents.find("defect-indicators.json");
    if (document == package.documents.end()) {
        return result;
    }
    const auto& catalogs = document->second["definitions"];
    if (!catalogs.isArray()) {
        return result;
    }
    for (const auto& catalog : catalogs) {
        const auto& applicable = catalog["applicable_component_ids"];
        const auto& indicators = catalog["indicators"];
        if (!applicable.isArray() || !indicators.isArray()) {
            continue;
        }
        for (const auto& indicator : indicators) {
            if (indicator["id"].isString()) {
                result.emplace(
                    indicator["id"].asString(),
                    DefectRuleView{&indicator, &applicable});
            }
        }
    }
    return result;
}

struct WeightProfileView {
    const StandardDefinition* profile{nullptr};
    std::map<std::string, double> component_weights;
    std::map<std::string, StructurePart> component_parts;
    std::map<StructurePart, double> structure_weights;
};

std::optional<WeightProfileView> weight_profile(
    const StandardPackage& package,
    const std::string& bridge_type_id) {
    WeightProfileView result;
    for (const auto& [id, candidate] : package.definitions) {
        if (id.starts_with("h21.weight_profile.") &&
            candidate.payload["bridge_type_id"].isString() &&
            candidate.payload["bridge_type_id"].asString() == bridge_type_id) {
            result.profile = &candidate;
            break;
        }
    }
    if (result.profile == nullptr) {
        return std::nullopt;
    }

    for (const auto& reference : result.profile->references) {
        if (!reference.starts_with("h21.weight_set.")) {
            continue;
        }
        const auto* rule = definition(package, reference);
        if (rule == nullptr || !rule->payload["weights"].isArray()) {
            return std::nullopt;
        }
        if (reference == kStructureWeightRule) {
            double total = 0.0;
            for (const auto& item : rule->payload["weights"]) {
                const auto parsed = parse_structure_part(item["structure_part"].asString());
                if (!parsed.has_value() || !item["value"].isNumeric()) {
                    return std::nullopt;
                }
                result.structure_weights[*parsed] = item["value"].asDouble();
                total += item["value"].asDouble();
            }
            if (std::abs(total - 1.0) > 1e-9) {
                return std::nullopt;
            }
            continue;
        }

        const auto parsed_part = parse_structure_part(rule->payload["structure_part"].asString());
        if (!parsed_part.has_value()) {
            return std::nullopt;
        }
        double total = 0.0;
        for (const auto& item : rule->payload["weights"]) {
            if (!item["component_id"].isString() || !item["value"].isNumeric()) {
                return std::nullopt;
            }
            const auto component_id = item["component_id"].asString();
            result.component_weights[component_id] = item["value"].asDouble();
            result.component_parts[component_id] = *parsed_part;
            total += item["value"].asDouble();
        }
        if (std::abs(total - 1.0) > 1e-9) {
            return std::nullopt;
        }
    }
    return result;
}

std::optional<double> count_factor(
    const StandardDefinition& rule,
    std::size_t component_count) {
    if (component_count == 0 || !rule.payload["points"].isArray()) {
        return std::nullopt;
    }
    if (component_count == 1) {
        return std::numeric_limits<double>::infinity();
    }

    struct Point {
        std::size_t n;
        double t;
        bool applies_to_or_above;
    };
    std::vector<Point> points;
    for (const auto& item : rule.payload["points"]) {
        if (!item["n"].isUInt() || item["t"].isString() || !item["t"].isNumeric()) {
            continue;
        }
        const auto t = item["t"].asDouble();
        if (!std::isfinite(t) || t <= 0.0) {
            return std::nullopt;
        }
        points.push_back({
            static_cast<std::size_t>(item["n"].asUInt()),
            t,
            item["applies_to_or_above"].asBool(),
        });
    }
    std::sort(points.begin(), points.end(), [](const Point& left, const Point& right) {
        return left.n < right.n;
    });
    for (const auto& point : points) {
        if (point.n == component_count ||
            (point.applies_to_or_above && component_count >= point.n)) {
            return point.t;
        }
    }
    if (!rule.payload["interpolate_missing"].asBool()) {
        return std::nullopt;
    }
    for (std::size_t index = 1; index < points.size(); ++index) {
        if (points[index - 1].n < component_count && component_count < points[index].n) {
            const auto ratio = static_cast<double>(component_count - points[index - 1].n) /
                               static_cast<double>(points[index].n - points[index - 1].n);
            return points[index - 1].t + ratio * (points[index].t - points[index - 1].t);
        }
    }
    return std::nullopt;
}

Json::Value doubles_json(const std::vector<double>& values) {
    Json::Value result(Json::arrayValue);
    for (const auto value : values) {
        result.append(value);
    }
    return result;
}

std::optional<TriggeredControlResult> control_result(
    const StandardPackage& package,
    std::string_view control_id,
    std::optional<int> derived_grade = std::nullopt) {
    const auto* control = definition(package, control_id);
    if (control == nullptr) {
        return std::nullopt;
    }
    TriggeredControlResult result;
    result.control_id = control->id;
    result.source_reference = source_reference(*control);
    result.label = control->payload["label"].asString();
    if (derived_grade.has_value()) {
        result.result_grade = derived_grade;
    } else if (control->payload["result_grade"].isInt()) {
        result.result_grade = control->payload["result_grade"].asInt();
    }
    return result;
}

}  // namespace

std::optional<H21ComponentScoreResult> compute_h21_component_score(
    const std::vector<double>& deductions) {
    if (deductions.empty()) {
        return std::nullopt;
    }
    for (const auto value : deductions) {
        if (!std::isfinite(value) || value < 0.0 || value > 100.0) {
            return std::nullopt;
        }
    }

    auto ordered = deductions;
    std::sort(ordered.begin(), ordered.end(), std::greater<double>());
    if (ordered.front() == 100.0) {
        return H21ComponentScoreResult{0.0, std::move(ordered)};
    }

    double total = 0.0;
    for (std::size_t index = 1; index <= ordered.size(); ++index) {
        const auto deduction = ordered[index - 1];
        const auto u = index == 1
            ? deduction
            : deduction / (100.0 * std::sqrt(static_cast<double>(index))) * (100.0 - total);
        total += u;
    }
    return H21ComponentScoreResult{100.0 - total, std::move(ordered)};
}

H21Evaluator::H21Evaluator(StandardPackage package) : package_(std::move(package)) {}

std::string H21Evaluator::algorithm_id() const {
    return package_.manifest.algorithm_id;
}

const StandardManifest& H21Evaluator::metadata() const noexcept {
    return package_.manifest;
}

std::vector<AssessmentIssue> H21Evaluator::validate(const BridgeAssessmentInput& input) const {
    return evaluate(input).issues;
}

std::optional<int> H21Evaluator::classify_grade(double score) const {
    if (!std::isfinite(score)) {
        return std::nullopt;
    }
    const auto* rule = definition(package_, kGradeRule);
    if (rule == nullptr || !rule->payload["grades"].isArray()) {
        return std::nullopt;
    }
    for (const auto& grade : rule->payload["grades"]) {
        if (!grade["grade"].isInt() || !grade["minimum"].isNumeric() ||
            !grade["maximum"].isNumeric()) {
            return std::nullopt;
        }
        const auto minimum = grade["minimum"].asDouble();
        const auto maximum = grade["maximum"].asDouble();
        const auto upper_matches = grade["maximum_inclusive"].asBool()
            ? score <= maximum
            : score < maximum;
        if (score >= minimum && upper_matches) {
            return grade["grade"].asInt();
        }
    }
    return std::nullopt;
}

AssessmentOutcome H21Evaluator::evaluate(const BridgeAssessmentInput& input) const {
    AssessmentOutcome outcome;
    if (package_.manifest.algorithm_id != "jtg-h21-2011") {
        outcome.issues.push_back(issue(
            "algorithm_unsupported", "评定器与规则包声明的算法不匹配。", {},
            package_.manifest.algorithm_id));
        return outcome;
    }
    if (package_.manifest.family != StandardFamily::technical_condition ||
        package_.manifest.standard_id != "JTG_T_H21_2011") {
        outcome.issues.push_back(issue(
            "standard_identity_mismatch", "H21 评定器不能解释其他规范身份的规则包。"));
        return outcome;
    }
    const auto* bridge_type = definition(package_, input.bridge_type_id);
    if (bridge_type == nullptr || !input.bridge_type_id.starts_with("h21.bridge_type.")) {
        outcome.issues.push_back(issue(
            "bridge_type_unsupported", "当前规范包不支持该桥型。", input.bridge_type_id));
        return outcome;
    }

    static constexpr std::array required_rules = {
        kComponentScoreRule,
        kCategoryScoreRule,
        kCountFactorRule,
        kPartScoreRule,
        kOverallScoreRule,
        kGradeRule,
        kStructureWeightRule,
        kWorstMajorControl,
        kGrade3CombinationControl,
    };
    for (const auto rule_id : required_rules) {
        if (definition(package_, rule_id) == nullptr) {
            outcome.issues.push_back(issue(
                "rule_missing", "规范包缺少完成评定所需的规则。", {}, std::string(rule_id)));
        }
    }
    const auto profile = weight_profile(package_, input.bridge_type_id);
    if (!profile.has_value()) {
        outcome.issues.push_back(issue(
            "weight_profile_missing", "规范包缺少该桥型的完整权重配置。", input.bridge_type_id));
    }
    if (!outcome.issues.empty()) {
        return outcome;
    }

    const auto algorithm_matches = [&](std::string_view rule_id, std::string_view expected) {
        const auto* rule = definition(package_, rule_id);
        return rule != nullptr && rule->payload["algorithm"].isString() &&
               rule->payload["algorithm"].asString() == expected;
    };
    if (!algorithm_matches(kComponentScoreRule, "ordered_cumulative_deduction") ||
        !algorithm_matches(kCategoryScoreRule, "mean_minus_lowest_deviation_over_t") ||
        !algorithm_matches(kPartScoreRule, "weighted_sum_of_component_categories") ||
        !algorithm_matches(kOverallScoreRule, "weighted_sum_of_structure_parts")) {
        outcome.issues.push_back(issue(
            "algorithm_unsupported", "规范包声明了当前评定器无法解释的计算算法。"));
        return outcome;
    }
    const auto* component_algorithm = definition(package_, kComponentScoreRule);
    const auto* category_algorithm = definition(package_, kCategoryScoreRule);
    if (component_algorithm->payload["sort"].asString() != "deduction_descending" ||
        !component_algorithm->payload["zero_when_any_deduction_is_100"].isBool() ||
        !component_algorithm->payload["zero_when_any_deduction_is_100"].asBool() ||
        !component_algorithm->payload["score_range"].isArray() ||
        component_algorithm->payload["score_range"].size() != 2u ||
        !component_algorithm->payload["score_range"][0].isNumeric() ||
        !component_algorithm->payload["score_range"][1].isNumeric() ||
        component_algorithm->payload["score_range"][0].asDouble() != 0.0 ||
        component_algorithm->payload["score_range"][1].asDouble() != 100.0) {
        outcome.issues.push_back(issue(
            "algorithm_contract_invalid",
            "构件评分规则缺少当前算法所需的排序、边界或 DP100 语义。",
            {},
            component_algorithm->id));
        return outcome;
    }
    const auto& passthrough_contract = category_algorithm->payload[
        "major_component_low_score_passthrough"];
    if (!passthrough_contract.isObject() ||
        !passthrough_contract["minimum"].isNumeric() ||
        !passthrough_contract["maximum_exclusive"].isNumeric()) {
        outcome.issues.push_back(issue(
            "algorithm_contract_invalid",
            "部件类别评分规则缺少主要部件低分直取语义。",
            {},
            category_algorithm->id));
        return outcome;
    }
    if (!bridge_type->payload["major_component_ids"].isArray() ||
        bridge_type->payload["major_component_ids"].empty()) {
        outcome.issues.push_back(issue(
            "major_component_classification_missing",
            "规范包缺少该桥型的主要部件分类。",
            input.bridge_type_id,
            bridge_type->id));
        return outcome;
    }
    for (const auto& major_id : bridge_type->payload["major_component_ids"]) {
        if (!major_id.isString() || definition(package_, major_id.asString()) == nullptr) {
            outcome.issues.push_back(issue(
                "major_component_classification_invalid",
                "规范包的主要部件分类引用无效。",
                input.bridge_type_id,
                bridge_type->id));
            return outcome;
        }
    }

    const auto defects = defect_rules(package_);
    if (defects.empty()) {
        outcome.issues.push_back(issue(
            "defect_catalog_missing", "规范包缺少病害指标目录。"));
        return outcome;
    }

    std::set<std::string> instance_ids;
    std::map<std::string, std::vector<const ComponentAssessmentInput*>> category_inputs;
    std::map<StructurePart, std::size_t> part_counts;
    for (const auto& component_input : input.components) {
        if (component_input.component_instance_id.empty() ||
            !instance_ids.insert(component_input.component_instance_id).second) {
            outcome.issues.push_back(issue(
                "component_instance_invalid",
                "实际构件编号必须非空且在本次评定中唯一。",
                component_input.component_instance_id));
            continue;
        }
        const auto* component_rule = definition(package_, component_input.component_type_id);
        if (component_rule == nullptr ||
            !component_input.component_type_id.starts_with("h21.component.")) {
            outcome.issues.push_back(issue(
                "component_type_unknown",
                "实际构件未映射到当前规范包中的构件类别。",
                component_input.component_instance_id,
                component_input.component_type_id));
            continue;
        }
        if (!string_array_contains(component_rule->payload["bridge_type_ids"], input.bridge_type_id)) {
            outcome.issues.push_back(issue(
                "component_not_applicable",
                "构件类别不适用于所选桥型。",
                component_input.component_instance_id,
                component_input.component_type_id));
            continue;
        }
        const auto configured_part = profile->component_parts.find(component_input.component_type_id);
        if (configured_part == profile->component_parts.end()) {
            outcome.issues.push_back(issue(
                "component_weight_missing",
                "该桥型的权重配置未包含此构件类别。",
                component_input.component_instance_id,
                component_input.component_type_id));
            continue;
        }
        const auto taxonomy_part = parse_structure_part(
            component_rule->payload["structure_part"].asString());
        if (!taxonomy_part.has_value() || *taxonomy_part != configured_part->second) {
            outcome.issues.push_back(issue(
                "component_hierarchy_mismatch",
                "构件分类与桥型权重层级不一致。",
                component_input.component_instance_id,
                component_input.component_type_id));
            continue;
        }
        category_inputs[component_input.component_type_id].push_back(&component_input);
        ++part_counts[*taxonomy_part];

        std::set<std::string> component_defect_ids;
        for (const auto& defect_input : component_input.defects) {
            if (!component_defect_ids.insert(defect_input.defect_indicator_id).second) {
                outcome.issues.push_back(issue(
                    "defect_indicator_duplicate",
                    "同一实际构件的同一种病害指标只能提供一个汇总标度。",
                    component_input.component_instance_id,
                    defect_input.defect_indicator_id));
                continue;
            }
            const auto defect = defects.find(defect_input.defect_indicator_id);
            if (defect == defects.end()) {
                outcome.issues.push_back(issue(
                    "defect_indicator_unknown",
                    "病害指标不属于当前规范包。",
                    component_input.component_instance_id,
                    defect_input.defect_indicator_id));
                continue;
            }
            if (!string_array_contains(
                    *defect->second.applicable_component_ids,
                    component_input.component_type_id)) {
                outcome.issues.push_back(issue(
                    "defect_not_applicable",
                    "病害指标不适用于该构件类别。",
                    component_input.component_instance_id,
                    defect_input.defect_indicator_id));
            }
            const auto& allowed_scales = (*defect->second.indicator)["allowed_scales"];
            const auto scale_allowed = std::any_of(
                allowed_scales.begin(), allowed_scales.end(), [&](const Json::Value& value) {
                    return value.isInt() && value.asInt() == defect_input.scale;
                });
            if (!scale_allowed) {
                outcome.issues.push_back(issue(
                    "defect_scale_invalid",
                    "病害标度不在该指标允许范围内。",
                    component_input.component_instance_id,
                    defect_input.defect_indicator_id));
            }
            const auto rule_id = (*defect->second.indicator)["deduction_rule_id"].asString();
            const auto* deduction_rule = definition(package_, rule_id);
            if (deduction_rule == nullptr) {
                outcome.issues.push_back(issue(
                    "rule_missing",
                    "病害指标引用的扣分规则不存在。",
                    component_input.component_instance_id,
                    rule_id));
            } else {
                const auto scale_key = std::to_string(defect_input.scale);
                if (!deduction_rule->payload["points"].isObject() ||
                    !deduction_rule->payload["points"].isMember(scale_key) ||
                    !deduction_rule->payload["points"][scale_key].isNumeric() ||
                    !std::isfinite(deduction_rule->payload["points"][scale_key].asDouble())) {
                    outcome.issues.push_back(issue(
                        "deduction_point_missing",
                        "扣分规则未提供该病害标度的有效扣分值。",
                        component_input.component_instance_id,
                        rule_id));
                }
            }
        }
    }

    for (const auto part : {
             StructurePart::superstructure,
             StructurePart::substructure,
             StructurePart::deck_system}) {
        if (!part_counts.contains(part)) {
            outcome.issues.push_back(issue(
                "structure_hierarchy_incomplete",
                "实际构件清单必须覆盖上部结构、下部结构和桥面系。",
                to_string(part)));
        }
    }
    std::set<std::string> requested_controls;
    for (const auto& control_id : input.triggered_control_ids) {
        const auto* control = definition(package_, control_id);
        if (!requested_controls.insert(control_id).second || control == nullptr ||
            !control_id.starts_with("h21.control.5.") ||
            !control->payload["result_grade"].isInt()) {
            outcome.issues.push_back(issue(
                "control_condition_unknown",
                "单项控制事实不是当前规范包可直接触发的控制条件。",
                control_id,
                control_id));
        }
    }
    if (!outcome.issues.empty()) {
        return outcome;
    }

    BridgeAssessmentResult result;
    result.standard_id = package_.manifest.standard_id;
    result.package_version = package_.manifest.package_version;
    result.bridge_type_id = input.bridge_type_id;

    const auto major_ids = bridge_type->payload["major_component_ids"];
    const auto* component_score_rule = definition(package_, kComponentScoreRule);
    const auto* category_score_rule = definition(package_, kCategoryScoreRule);
    const auto* count_factor_rule = definition(package_, kCountFactorRule);
    const auto* part_score_rule = definition(package_, kPartScoreRule);
    const auto* overall_score_rule = definition(package_, kOverallScoreRule);
    const auto* grade_rule = definition(package_, kGradeRule);
    const auto* structure_weight_rule = definition(package_, kStructureWeightRule);

    std::map<StructurePart, std::vector<ComponentCategoryAssessmentResult>> categories_by_part;
    for (const auto& [component_type_id, instances] : category_inputs) {
        ComponentCategoryAssessmentResult category;
        category.component_type_id = component_type_id;
        category.structure_part = profile->component_parts.at(component_type_id);
        category.major = string_array_contains(major_ids, component_type_id);
        category.configured_weight = profile->component_weights.at(component_type_id);

        std::vector<double> component_scores;
        for (const auto* component_input : instances) {
            ComponentAssessmentResult component_result;
            component_result.component_instance_id = component_input->component_instance_id;
            component_result.component_type_id = component_type_id;
            component_result.structure_part = category.structure_part;
            component_result.major = category.major;
            std::vector<double> deductions;
            for (const auto& defect_input : component_input->defects) {
                const auto defect = defects.at(defect_input.defect_indicator_id);
                const auto rule_id = (*defect.indicator)["deduction_rule_id"].asString();
                const auto* deduction_rule = definition(package_, rule_id);
                const auto deduction = deduction_rule->payload["points"]
                    [std::to_string(defect_input.scale)].asDouble();
                deductions.push_back(deduction);
                component_result.defects.push_back({
                    defect_input.defect_indicator_id,
                    defect_input.scale,
                    deduction,
                    rule_id,
                    (*defect.indicator)["source_table"].asString(),
                });

                AssessmentTraceEntry trace;
                trace.step = "defect_deduction";
                trace.rule_id = rule_id;
                trace.entity_id = component_input->component_instance_id + ":" +
                                  defect_input.defect_indicator_id;
                trace.source_reference = (*defect.indicator)["source_table"].asString();
                trace.inputs["scale"] = defect_input.scale;
                trace.output["deduction"] = deduction;
                result.trace.push_back(std::move(trace));
            }

            if (deductions.empty()) {
                component_result.score = 100.0;
            } else {
                const auto scored = compute_h21_component_score(deductions);
                if (!scored.has_value()) {
                    outcome.issues.push_back(issue(
                        "component_score_invalid",
                        "构件扣分无法形成有效评分。",
                        component_input->component_instance_id,
                        component_score_rule->id));
                    return outcome;
                }
                component_result.score = scored->score;
                component_result.ordered_deductions = scored->ordered_deductions;
            }
            component_scores.push_back(component_result.score);

            AssessmentTraceEntry component_trace;
            component_trace.step = "component_score";
            component_trace.rule_id = component_score_rule->id;
            component_trace.entity_id = component_input->component_instance_id;
            component_trace.source_reference = source_reference(*component_score_rule);
            component_trace.inputs["ordered_deductions"] =
                doubles_json(component_result.ordered_deductions);
            component_trace.output["score"] = component_result.score;
            result.trace.push_back(std::move(component_trace));
            category.components.push_back(std::move(component_result));
        }

        category.mean_component_score = std::accumulate(
            component_scores.begin(), component_scores.end(), 0.0) /
            static_cast<double>(component_scores.size());
        category.minimum_component_score = *std::min_element(
            component_scores.begin(), component_scores.end());
        const auto t = count_factor(*count_factor_rule, component_scores.size());
        if (!t.has_value()) {
            outcome.issues.push_back(issue(
                "component_count_factor_missing",
                "规范包无法为构件数量确定 t 值。",
                component_type_id,
                count_factor_rule->id));
            return outcome;
        }
        if (std::isfinite(*t)) {
            category.component_count_factor = *t;
        }
        const auto& passthrough = category_score_rule->payload[
            "major_component_low_score_passthrough"];
        const auto passthrough_minimum = passthrough["minimum"].asDouble();
        const auto passthrough_maximum = passthrough["maximum_exclusive"].asDouble();
        category.low_score_passthrough = category.major &&
            category.minimum_component_score >= passthrough_minimum &&
            category.minimum_component_score < passthrough_maximum;
        if (category.low_score_passthrough) {
            category.score = category.minimum_component_score;
        } else if (std::isinf(*t)) {
            category.score = category.mean_component_score;
        } else {
            category.score = category.mean_component_score -
                (100.0 - category.minimum_component_score) / *t;
        }
        const auto grade = classify_grade(category.score);
        if (!grade.has_value()) {
            outcome.issues.push_back(issue(
                "grade_rule_invalid",
                "部件类别评分无法匹配等级界限。",
                component_type_id,
                grade_rule->id));
            return outcome;
        }
        category.grade = *grade;

        AssessmentTraceEntry category_trace;
        category_trace.step = "component_category_score";
        category_trace.rule_id = category_score_rule->id;
        category_trace.entity_id = component_type_id;
        category_trace.source_reference = source_reference(*category_score_rule);
        category_trace.inputs["mean"] = category.mean_component_score;
        category_trace.inputs["minimum"] = category.minimum_component_score;
        if (category.component_count_factor.has_value()) {
            category_trace.inputs["t"] = *category.component_count_factor;
        } else {
            category_trace.inputs["t"] = "infinity";
        }
        category_trace.inputs["major"] = category.major;
        category_trace.output["score"] = category.score;
        category_trace.output["grade"] = category.grade;
        category_trace.output["low_score_passthrough"] = category.low_score_passthrough;
        result.trace.push_back(std::move(category_trace));
        categories_by_part[category.structure_part].push_back(std::move(category));
    }

    for (const auto part_id : {
             StructurePart::superstructure,
             StructurePart::substructure,
             StructurePart::deck_system}) {
        auto& categories = categories_by_part.at(part_id);
        const auto configured_total = std::accumulate(
            categories.begin(), categories.end(), 0.0,
            [](double total, const ComponentCategoryAssessmentResult& category) {
                return total + category.configured_weight;
            });
        if (!std::isfinite(configured_total) || configured_total <= 0.0) {
            outcome.issues.push_back(issue(
                "weight_rule_invalid",
                "现有部件的权重无法按比例重新分配。",
                to_string(part_id),
                profile->profile->id));
            return outcome;
        }

        StructurePartAssessmentResult part_result;
        part_result.structure_part = part_id;
        const auto overall_weight = profile->structure_weights.find(part_id);
        if (overall_weight == profile->structure_weights.end() ||
            !std::isfinite(overall_weight->second)) {
            outcome.issues.push_back(issue(
                "weight_rule_invalid",
                "规范包缺少结构层总体权重。",
                to_string(part_id),
                structure_weight_rule->id));
            return outcome;
        }
        part_result.overall_weight = overall_weight->second;
        for (auto& category : categories) {
            category.effective_weight = category.configured_weight / configured_total;
            part_result.score += category.score * category.effective_weight;
        }
        const auto grade = classify_grade(part_result.score);
        if (!grade.has_value()) {
            outcome.issues.push_back(issue(
                "grade_rule_invalid",
                "结构层评分无法匹配等级界限。",
                to_string(part_id),
                grade_rule->id));
            return outcome;
        }
        part_result.grade = *grade;
        part_result.categories = std::move(categories);

        AssessmentTraceEntry part_trace;
        part_trace.step = "structure_part_score";
        part_trace.rule_id = part_score_rule->id;
        part_trace.entity_id = to_string(part_id);
        part_trace.source_reference = source_reference(*part_score_rule);
        for (const auto& category : part_result.categories) {
            Json::Value item(Json::objectValue);
            item["component_type_id"] = category.component_type_id;
            item["score"] = category.score;
            item["effective_weight"] = category.effective_weight;
            part_trace.inputs["categories"].append(std::move(item));
        }
        part_trace.output["score"] = part_result.score;
        part_trace.output["grade"] = part_result.grade;
        result.trace.push_back(std::move(part_trace));
        result.structure_parts.push_back(std::move(part_result));
    }

    for (const auto& part_result : result.structure_parts) {
        result.overall_score += part_result.score * part_result.overall_weight;
    }
    const auto calculated_grade = classify_grade(result.overall_score);
    if (!calculated_grade.has_value()) {
        outcome.issues.push_back(issue(
            "grade_rule_invalid",
            "桥梁总体评分无法匹配等级界限。",
            input.bridge_type_id,
            grade_rule->id));
        return outcome;
    }
    result.calculated_grade = *calculated_grade;
    result.final_grade = *calculated_grade;

    AssessmentTraceEntry overall_trace;
    overall_trace.step = "overall_score";
    overall_trace.rule_id = overall_score_rule->id;
    overall_trace.entity_id = input.bridge_type_id;
    overall_trace.source_reference = source_reference(*overall_score_rule);
    for (const auto& part_result : result.structure_parts) {
        Json::Value item(Json::objectValue);
        item["structure_part"] = to_string(part_result.structure_part);
        item["score"] = part_result.score;
        item["weight"] = part_result.overall_weight;
        overall_trace.inputs["structure_parts"].append(std::move(item));
    }
    overall_trace.output["score"] = result.overall_score;
    result.trace.push_back(std::move(overall_trace));

    const auto part_grade = [&](StructurePart expected) {
        const auto found = std::find_if(
            result.structure_parts.begin(), result.structure_parts.end(),
            [&](const StructurePartAssessmentResult& part) {
                return part.structure_part == expected;
            });
        return found->grade;
    };
    if (part_grade(StructurePart::superstructure) == 3 &&
        part_grade(StructurePart::substructure) == 3 &&
        part_grade(StructurePart::deck_system) == 4 &&
        result.overall_score >= 40.0 && result.overall_score < 60.0) {
        result.final_grade = 3;
        result.triggered_controls.push_back(
            *control_result(package_, kGrade3CombinationControl, 3));
    }

    for (const auto& control_id : input.triggered_control_ids) {
        auto control = *control_result(package_, control_id);
        result.final_grade = std::max(result.final_grade, *control.result_grade);
        result.triggered_controls.push_back(std::move(control));
    }

    if (input.worst_major_component_affects_safety) {
        int worst_grade = 0;
        for (const auto& part_result : result.structure_parts) {
            for (const auto& category : part_result.categories) {
                if (category.major) {
                    worst_grade = std::max(worst_grade, category.grade);
                }
            }
        }
        if (worst_grade >= 4) {
            result.final_grade = std::max(result.final_grade, worst_grade);
            result.triggered_controls.push_back(
                *control_result(package_, kWorstMajorControl, worst_grade));
        }
    }

    AssessmentTraceEntry grade_trace;
    grade_trace.step = "grade";
    grade_trace.rule_id = grade_rule->id;
    grade_trace.entity_id = input.bridge_type_id;
    grade_trace.source_reference = source_reference(*grade_rule);
    grade_trace.inputs["overall_score"] = result.overall_score;
    grade_trace.inputs["triggered_control_count"] =
        static_cast<Json::UInt64>(result.triggered_controls.size());
    grade_trace.output["calculated_grade"] = result.calculated_grade;
    grade_trace.output["final_grade"] = result.final_grade;
    result.trace.push_back(std::move(grade_trace));

    for (const auto& control : result.triggered_controls) {
        AssessmentTraceEntry control_trace;
        control_trace.step = "control";
        control_trace.rule_id = control.control_id;
        control_trace.entity_id = input.bridge_type_id;
        control_trace.source_reference = control.source_reference;
        control_trace.inputs["calculated_grade"] = result.calculated_grade;
        if (control.result_grade.has_value()) {
            control_trace.output["result_grade"] = *control.result_grade;
        }
        result.trace.push_back(std::move(control_trace));
    }

    result.explanation = explain(result);
    outcome.result = std::move(result);
    return outcome;
}

std::string H21Evaluator::explain(const BridgeAssessmentResult& result) const {
    std::ostringstream text;
    text << std::fixed << std::setprecision(2);
    text << "依据 " << package_.manifest.standard_code << "（规则包 "
         << package_.manifest.package_version << "）计算：全桥技术状况评分为 "
         << result.overall_score << "，分数对应 " << result.calculated_grade << " 类";
    if (result.final_grade != result.calculated_grade) {
        text << "；应用 " << result.triggered_controls.size()
             << " 项控制规则后，最终评定为 " << result.final_grade << " 类";
    } else {
        text << "，最终评定为 " << result.final_grade << " 类";
    }
    text << "。";
    return text.str();
}

}  // namespace bridge_report::standards
