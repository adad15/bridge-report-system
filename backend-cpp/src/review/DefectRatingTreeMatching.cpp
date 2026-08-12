#include "bridge_report/review/DefectRatingTreeMatching.hpp"

#include <utility>

#include "bridge_report/review/JsonAccessors.hpp"

namespace bridge_report::review {
namespace {

using rating_tree::RatingTreeMatchOutcome;

// 自动绑定只来自来源分组+指标的显式对表；文字方式不再参与。
bool is_auto_match_method(const std::string& method) {
    return method == "source_indicator";
}

void clear_rating_tree_fields(
    Json::Value& defect,
    const std::string& rating_tree_version_id) {
    defect["rating_tree_version_id"] = rating_tree_version_id;
    defect["rating_tree_node_id"] = Json::Value();
    defect["standard_defect_indicator_id"] = Json::Value();
    defect["rating_tree_match_method"] = Json::Value();
    defect["rating_tree_match_evidence"] = Json::Value();
}

void write_auto_binding(
    Json::Value& defect,
    const rating_tree::RatingTreeMatchResult& result) {
    defect["rating_tree_node_id"] = *result.node_id;
    defect["standard_defect_indicator_id"] = result.h21_indicator_id.has_value()
        ? Json::Value(*result.h21_indicator_id)
        : Json::Value();
    defect["rating_tree_match_method"] = result.match_method;
    // 依据为空就写 null：契约允许缺省，页面据此隐藏那一行。
    defect["rating_tree_match_evidence"] = result.match_evidence.empty()
        ? Json::Value()
        : Json::Value(result.match_evidence);
}

void count(DefectMatchStats& stats, const RatingTreeMatchOutcome outcome) {
    switch (outcome) {
        case RatingTreeMatchOutcome::auto_bound: ++stats.auto_bound; return;
        case RatingTreeMatchOutcome::candidates: ++stats.candidates; return;
        case RatingTreeMatchOutcome::composite: ++stats.composite; return;
        case RatingTreeMatchOutcome::prerequisite_missing:
            ++stats.prerequisite_missing;
            return;
        case RatingTreeMatchOutcome::service_error: ++stats.failed; return;
        case RatingTreeMatchOutcome::unmatched: ++stats.unmatched; return;
    }
}

}  // namespace

bool defect_is_protected_from_auto_match(const Json::Value& defect) {
    const auto review_status = string_member_or_empty(defect, "review_status");
    const auto group_status = string_member_or_empty(defect, "group_review_status");
    const auto method = string_member_or_empty(defect, "rating_tree_match_method");
    if (review_status == "已确认" || review_status == "已忽略") return true;
    if (group_status == "已确认") return true;
    // 人工选择的节点在普通文字修改后继续保留，只有用户自己能改。
    return method == "manual" &&
        !string_member_or_empty(defect, "rating_tree_node_id").empty();
}

std::optional<rating_tree::RatingTreeMatchInput> build_defect_match_input(
    const Json::Value& defect,
    const std::string& technical_standard_package_id,
    const std::optional<inventory::InventoryRevision>& latest_revision,
    std::string& reason_code,
    std::string& reason_message) {
    const auto component_id = string_member_or_empty(defect, "bridge_component_id");
    if (component_id.empty()) {
        reason_code = rating_tree::kReasonComponentNotBound;
        reason_message = "该病害尚未绑定实际构件，无法确定适用的评定树病害范围。";
        return std::nullopt;
    }
    if (!latest_revision.has_value()) {
        reason_code = rating_tree::kReasonComponentCategoryUnmapped;
        reason_message = "当前桥梁没有已确认的构件台账，无法解析规范构件类别。";
        return std::nullopt;
    }
    for (const auto& entry : latest_revision->entries) {
        if (!entry.is_active || entry.bridge_component_id != component_id) continue;
        for (const auto& mapping : entry.mappings) {
            if (mapping.is_active && mapping.confirmation_status == "已确认" &&
                mapping.standard_package_id == technical_standard_package_id) {
                rating_tree::RatingTreeMatchInput input;
                input.bridge_type_id = mapping.standard_bridge_type_id;
                input.component_category_id = mapping.standard_component_category_id;
                input.defect_type = string_member_or_empty(defect, "defect_type");
                input.defect_description =
                    string_member_or_empty(defect, "defect_description");
                input.defect_location =
                    string_member_or_empty(defect, "defect_location");
                input.source_defect_group_id =
                    string_member_or_empty(defect, "source_defect_group_id");
                input.source_defect_group_number =
                    string_member_or_empty(defect, "source_defect_group_number");
                input.source_defect_indicator_id =
                    string_member_or_empty(defect, "source_defect_indicator_id");
                input.source_defect_indicator_number =
                    string_member_or_empty(
                        defect, "source_defect_indicator_number");
                return input;
            }
        }
    }
    reason_code = rating_tree::kReasonComponentCategoryUnmapped;
    reason_message = "该实际构件在当前规范包下没有已确认的规范类别映射。";
    return std::nullopt;
}

DefectMatchReport match_defect_rating_tree_nodes(
    Json::Value& draft,
    const std::string& rating_tree_version_id,
    const std::string& technical_standard_package_id,
    const rating_tree::EffectiveRatingTree& tree,
    const std::optional<inventory::InventoryRevision>& latest_revision,
    const DefectMatchScope& scope,
    const bool apply) {
    DefectMatchReport report;
    if (!draft["defects"].isArray()) return report;

    const rating_tree::RatingTreeResolver resolver;
    for (Json::ArrayIndex index = 0; index < draft["defects"].size(); ++index) {
        auto& defect = draft["defects"][index];
        const auto candidate_id = string_member_or_empty(defect, "candidate_id");
        if (scope.has_scope && !scope.candidate_ids.contains(candidate_id)) continue;

        ++report.stats.processed;
        DefectMatchRecord record;
        record.candidate_id = candidate_id;

        if (defect_is_protected_from_auto_match(defect)) {
            // 人工与已确认结果不参与自动匹配，也不被自动结果覆盖：只回传现状，
            // outcome 保持默认，页面按 skipped 渲染"人工选择/已确认"。
            record.skipped = true;
            const auto stored_node =
                string_member_or_empty(defect, "rating_tree_node_id");
            if (!stored_node.empty()) record.node_id = stored_node;
            record.match_method =
                string_member_or_empty(defect, "rating_tree_match_method");
            record.match_evidence =
                string_member_or_empty(defect, "rating_tree_match_evidence");
            ++report.stats.skipped;
            report.records.push_back(std::move(record));
            continue;
        }

        std::string reason_code;
        std::string reason_message;
        const auto input = build_defect_match_input(
            defect,
            technical_standard_package_id,
            latest_revision,
            reason_code,
            reason_message);
        if (!input.has_value()) {
            if (apply) clear_rating_tree_fields(defect, rating_tree_version_id);
            record.outcome = RatingTreeMatchOutcome::prerequisite_missing;
            record.reason_code = reason_code;
            record.reason_message = reason_message;
            count(report.stats, record.outcome);
            report.records.push_back(std::move(record));
            continue;
        }

        rating_tree::RatingTreeMatchResult result;
        try {
            result = resolver.resolve(tree, *input);
        } catch (...) {
            // 单条失败不得破坏同批其他记录，也不得被计成"无匹配结果"。
            result = {};
            result.outcome = RatingTreeMatchOutcome::service_error;
            result.reason_code = rating_tree::kReasonMatcherFailed;
            result.reason_message = "匹配服务执行失败，请稍后重试。";
        }

        if (apply) {
            clear_rating_tree_fields(defect, rating_tree_version_id);
            if (result.outcome == RatingTreeMatchOutcome::auto_bound &&
                result.node_id.has_value()) {
                write_auto_binding(defect, result);
            }
        }

        record.outcome = result.outcome;
        record.node_id = result.node_id;
        record.match_method = result.match_method;
        record.match_evidence = result.match_evidence;
        record.candidates = result.candidates;
        record.reason_code = result.reason_code;
        record.reason_message = result.reason_message;
        count(report.stats, record.outcome);
        report.records.push_back(std::move(record));
    }
    return report;
}

}  // namespace bridge_report::review
