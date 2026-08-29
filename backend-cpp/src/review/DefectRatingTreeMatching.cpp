#include "bridge_report/review/DefectRatingTreeMatching.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "bridge_report/review/JsonAccessors.hpp"

namespace bridge_report::review {
namespace {

using rating_tree::RatingTreeMatchOutcome;

// 自动绑定只来自来源分组+指标的显式对表；文字方式不再参与。
bool is_auto_match_method(const std::string& method) {
    return method == "source_indicator";
}

// 可确认视图里 candidate_id 是实例 id，来源病害身份在 source_candidate_id。缺这一项
// 就退回 candidate_id：手工新增的病害在解析表里建实例前只有来源身份。
std::string source_candidate_key(const Json::Value& defect) {
    const auto source_id = string_member_or_empty(defect, "source_candidate_id");
    return source_id.empty() ? string_member_or_empty(defect, "candidate_id") : source_id;
}

std::vector<std::string> sorted_candidate_ids(const DefectMatchRecord& record) {
    std::vector<std::string> ids;
    ids.reserve(record.candidates.size());
    for (const auto& candidate : record.candidates) ids.push_back(candidate.node_id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

// 两条实例的结论算不算"一致"。
//
// 比 outcome 和 node_id 不够：两条实例都停在 candidates 却给出不同候选集，或者都
// prerequisite_missing 却缺的前提不同，按那个口径会被判成一致，于是整行采用第一条的
// 候选与原因——而那份内容对另一条并不成立。凡是会影响用户判断的字段都要比，否则
// instances_disagree 就挡不住它本来要挡的那类分歧。
//
// 候选集按节点 id 排序后比：解析器给出的顺序不进入语义。
bool same_result(const DefectMatchRecord& a, const DefectMatchRecord& b) {
    return a.outcome == b.outcome && a.skipped == b.skipped &&
        a.node_id.value_or(std::string{}) == b.node_id.value_or(std::string{}) &&
        a.match_method == b.match_method &&
        a.match_evidence == b.match_evidence &&
        a.reason_code == b.reason_code &&
        a.reason_message == b.reason_message &&
        sorted_candidate_ids(a) == sorted_candidate_ids(b);
}

// 一条来源病害的多个实例合成它那一行的结论。
//
// 全体一致才敢把结论摆上去：展开出来的构件通常同类别、同结果，合并后与展开前看到的
// 完全一样。真出现分歧（各实例落在不同规范类别）时不能挑一个充数——页面这一行会被
// 当成整条病害的判断，写谁都是错的，只能明说需要逐个实例处理。
DefectMatchRecord merge_instance_records(std::vector<DefectMatchRecord>& records) {
    DefectMatchRecord merged = std::move(records.front());
    for (std::size_t index = 1; index < records.size(); ++index) {
        if (same_result(merged, records[index])) continue;
        DefectMatchRecord disagreement;
        disagreement.candidate_id = merged.candidate_id;
        disagreement.outcome = RatingTreeMatchOutcome::unmatched;
        disagreement.reason_code = rating_tree::kReasonInstancesDisagree;
        disagreement.reason_message =
            "展开后的各实际构件匹配结果不一致，请逐个实例选择评定树病害。";
        return disagreement;
    }
    return merged;
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
    const std::optional<inventory::InventoryRevision>& resolved_revision,
    std::string& reason_code,
    std::string& reason_message) {
    const auto component_id = string_member_or_empty(defect, "bridge_component_id");
    if (component_id.empty()) {
        reason_code = rating_tree::kReasonComponentNotBound;
        reason_message = "该病害尚未绑定实际构件，无法确定适用的评定树病害范围。";
        return std::nullopt;
    }
    if (!resolved_revision.has_value()) {
        reason_code = rating_tree::kReasonComponentCategoryUnmapped;
        reason_message = "当前桥梁没有已确认的构件台账，无法解析规范构件类别。";
        return std::nullopt;
    }
    for (const auto& entry : resolved_revision->entries) {
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
    const Json::Value& view,
    const std::string& technical_standard_package_id,
    const rating_tree::EffectiveRatingTree& tree,
    const std::optional<inventory::InventoryRevision>& resolved_revision,
    const DefectMatchScope& scope) {
    DefectMatchReport report;
    if (!view["defects"].isArray()) return report;

    // 视图里一条来源病害展开成几条实例就有几项，但校对页按来源病害显示一行
    // （§22.6），所以逐实例算完再按来源合并，一条来源病害只回一条记录。
    std::vector<std::string> source_order;
    std::map<std::string, std::vector<DefectMatchRecord>> by_source;

    const rating_tree::RatingTreeResolver resolver;
    for (const auto& defect : view["defects"]) {
        const auto source_id = source_candidate_key(defect);
        if (scope.has_scope && !scope.candidate_ids.contains(source_id)) continue;
        if (!by_source.contains(source_id)) source_order.push_back(source_id);

        DefectMatchRecord record;
        record.candidate_id = source_id;

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
            by_source[source_id].push_back(std::move(record));
            continue;
        }

        std::string reason_code;
        std::string reason_message;
        const auto input = build_defect_match_input(
            defect,
            technical_standard_package_id,
            resolved_revision,
            reason_code,
            reason_message);
        if (!input.has_value()) {
            record.outcome = RatingTreeMatchOutcome::prerequisite_missing;
            record.reason_code = reason_code;
            record.reason_message = reason_message;
            by_source[source_id].push_back(std::move(record));
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

        record.outcome = result.outcome;
        record.node_id = result.node_id;
        record.match_method = result.match_method;
        record.match_evidence = result.match_evidence;
        record.candidates = result.candidates;
        record.reason_code = result.reason_code;
        record.reason_message = result.reason_message;
        by_source[source_id].push_back(std::move(record));
    }

    for (const auto& source_id : source_order) {
        auto merged = merge_instance_records(by_source[source_id]);
        ++report.stats.processed;
        if (merged.skipped) {
            ++report.stats.skipped;
        } else {
            count(report.stats, merged.outcome);
        }
        report.records.push_back(std::move(merged));
    }
    return report;
}

}  // namespace bridge_report::review
