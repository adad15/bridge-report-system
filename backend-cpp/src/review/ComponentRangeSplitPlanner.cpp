#include "bridge_report/review/ComponentRangeSplitPlanner.hpp"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <utility>

#include "bridge_report/inventory/ComponentMatcher.hpp"
#include "bridge_report/inventory/ComponentRangeParser.hpp"

namespace bridge_report::review {
namespace {

std::string string_member(const Json::Value& object, const char* name) {
    return object.isObject() && object[name].isString()
        ? object[name].asString() : std::string();
}

std::string target_key(const ComponentRangeSplitTarget& target) {
    return target.part_name + "\n"
        + inventory::normalize_component_number(target.component_number);
}

bool is_ineligible(const Json::Value& defect) {
    if (!string_member(defect, "bridge_component_id").empty()) return true;
    return string_member(defect, "component_match_method") == "missing";
}

std::string resolved_structure_part(const std::string& part) {
    return part == "superstructure" ? "上部结构"
        : part == "substructure" ? "下部结构"
        : part == "deck_system" ? "桥面系"
        : part == "overall" ? "全桥" : "其他";
}

ComponentRangeSplitMatch analyze_match(
    const std::string& number,
    const std::string& part_name,
    const inventory::InventoryRevision& revision) {
    ComponentRangeSplitMatch result;
    result.component_number = number;
    const auto match = inventory::match_defect_component(
        inventory::DefectComponentText{number, part_name}, revision, {});
    result.candidate_component_ids = match.candidate_component_ids;
    if (match.matched_entry && match.matched_mapping) {
        result.bridge_component_id = match.matched_entry->bridge_component_id;
        result.standard_component_category_id =
            match.matched_mapping->standard_component_category_id;
        result.resolved_structure_part =
            resolved_structure_part(match.matched_mapping->structure_part);
        result.match_method = inventory::component_match_method_name(match.method);
    } else if (!match.candidate_component_ids.empty()) {
        result.match_method = inventory::component_match_method_name(match.method);
    }
    return result;
}

std::string unique_id(const std::string& desired, std::set<std::string>& occupied) {
    if (occupied.insert(desired).second) return desired;
    for (int suffix = 2;; ++suffix) {
        const auto candidate = desired + "_" + std::to_string(suffix);
        if (occupied.insert(candidate).second) return candidate;
    }
}

void append_split_warning(Json::Value& warnings, const std::string& candidate_id) {
    if (!warnings.isArray()) warnings = Json::Value(Json::arrayValue);
    Json::Value warning(Json::objectValue);
    warning["code"] = "component_range_split_review_required";
    warning["message"] = "该病害由构件范围拆分，请人工核对构件、病害和照片关联。";
    warning["severity"] = "warning";
    warning["target_candidate_id"] = candidate_id;
    warnings.append(std::move(warning));
}

void clear_binding(Json::Value& defect) {
    defect["bridge_component_id"] = Json::Value();
    defect["standard_component_category_id"] = Json::Value();
    defect["resolved_structure_part"] = Json::Value();
    defect["component_inventory_revision_id"] = Json::Value();
    defect["component_match_candidate_ids"] = Json::Value(Json::arrayValue);
    defect["component_match_method"] = Json::Value();
    defect["component_match_confirmed_by"] = Json::Value();
}

void apply_analyzed_match(
    Json::Value& defect,
    const ComponentRangeSplitMatch& match,
    const std::string& inventory_revision_id) {
    defect["component_inventory_revision_id"] = inventory_revision_id;
    defect["component_match_candidate_ids"] = Json::Value(Json::arrayValue);
    for (const auto& id : match.candidate_component_ids) {
        defect["component_match_candidate_ids"].append(id);
    }
    if (!match.bridge_component_id.empty()) {
        defect["bridge_component_id"] = match.bridge_component_id;
        defect["standard_component_category_id"] =
            match.standard_component_category_id;
        defect["resolved_structure_part"] = match.resolved_structure_part;
        defect["component_match_method"] = match.match_method;
    } else if (!match.candidate_component_ids.empty()) {
        defect["component_match_method"] = match.match_method;
    }
}

Json::Value split_origin(
    const std::string& source_id,
    const std::string& source_number,
    const std::string& expanded_number,
    int index,
    int count) {
    Json::Value origin(Json::objectValue);
    origin["operation_id"] = "__range_split_operation__";
    origin["source_candidate_id"] = source_id;
    origin["source_component_number"] = source_number;
    origin["expanded_component_number"] = expanded_number;
    origin["split_index"] = index;
    origin["split_count"] = count;
    origin["operated_by_user_id"] = "__range_split_user__";
    origin["operated_at"] = "1970-01-01T00:00:00Z";
    return origin;
}

void rewrite_targeted_warning(
    Json::Value warning,
    const std::string& old_id,
    const std::string& new_id,
    Json::Value& output) {
    if (string_member(warning, "target_candidate_id") != old_id) return;
    warning["target_candidate_id"] = new_id;
    output.append(std::move(warning));
}

ComponentRangeSplitPlan plan_from_analysis(const ComponentRangeSplitAnalysis& analysis) {
    ComponentRangeSplitPlan plan;
    plan.status = analysis.status;
    plan.error_code = analysis.error_code;
    plan.error_message = analysis.error_message;
    plan.rejected_target = analysis.rejected_target;
    plan.items = analysis.items;
    plan.totals = analysis.totals;
    return plan;
}

}  // namespace

ComponentRangeSplitAnalysis analyze_component_range_splits(
    const Json::Value& current,
    const inventory::InventoryRevision& revision,
    std::vector<ComponentRangeSplitTarget> targets,
    std::size_t max_range_count,
    std::size_t max_result_defects) {
    ComponentRangeSplitAnalysis analysis;
    analysis.inventory_revision_id = revision.id;
    if (targets.empty() || !current["defects"].isArray()) {
        analysis.status = ComponentRangeSplitPlanStatus::InvalidTarget;
        analysis.error_code = "component_range_split_invalid_target";
        analysis.error_message = "至少选择一个可拆分的构件范围。";
        return analysis;
    }

    std::sort(targets.begin(), targets.end(), [](const auto& left, const auto& right) {
        const auto left_number = inventory::normalize_component_number(left.component_number);
        const auto right_number = inventory::normalize_component_number(right.component_number);
        return std::tie(left.part_name, left_number, left.component_number)
            < std::tie(right.part_name, right_number, right.component_number);
    });
    targets.erase(std::unique(targets.begin(), targets.end(), [](const auto& a, const auto& b) {
        return target_key(a) == target_key(b);
    }), targets.end());

    std::unordered_map<std::string, int> photo_counts;
    if (current["photos"].isArray()) {
        for (const auto& photo : current["photos"]) {
            ++photo_counts[string_member(photo, "linked_defect_candidate_id")];
        }
    }

    for (const auto& target : targets) {
        ComponentRangeSplitWorkItem work;
        work.summary.target = target;
        const auto expansion =
            inventory::parse_component_range(target.component_number, max_range_count);
        if (expansion.status != inventory::ComponentRangeParseStatus::Ok) {
            analysis.status = expansion.status == inventory::ComponentRangeParseStatus::LimitExceeded
                ? ComponentRangeSplitPlanStatus::RangeLimitExceeded
                : ComponentRangeSplitPlanStatus::InvalidTarget;
            analysis.error_code = expansion.status
                    == inventory::ComponentRangeParseStatus::LimitExceeded
                ? "component_range_split_range_limit_exceeded"
                : "component_range_split_invalid_range";
            analysis.error_message = expansion.message;
            analysis.rejected_target = target;
            return analysis;
        }
        work.summary.expanded_component_count =
            static_cast<int>(expansion.numbers.size());

        const auto normalized_target =
            inventory::normalize_component_number(target.component_number);
        int source_photo_count = 0;
        for (const auto& defect : current["defects"]) {
            if (string_member(defect, "component_name") != target.part_name
                || inventory::normalize_component_number(
                       string_member(defect, "component_number")) != normalized_target) {
                continue;
            }
            if (is_ineligible(defect)) {
                analysis.status = ComponentRangeSplitPlanStatus::IneligibleTarget;
                analysis.error_code = "component_range_split_ineligible";
                analysis.error_message = "已绑定或已标记缺失的构件需先清除处理结果。";
                analysis.rejected_target = target;
                return analysis;
            }
            const auto source_id = string_member(defect, "candidate_id");
            work.source_candidate_ids.push_back(source_id);
            source_photo_count += photo_counts[source_id];
        }
        if (work.source_candidate_ids.empty()) {
            analysis.status = ComponentRangeSplitPlanStatus::InvalidTarget;
            analysis.error_code = "component_range_split_target_not_found";
            analysis.error_message = "所选构件范围已不存在。";
            analysis.rejected_target = target;
            return analysis;
        }

        work.summary.source_defect_count =
            static_cast<int>(work.source_candidate_ids.size());
        work.summary.result_defect_count = work.summary.source_defect_count
            * work.summary.expanded_component_count;
        if (analysis.totals.result_defect_count + work.summary.result_defect_count
            > static_cast<int>(max_result_defects)) {
            analysis.status = ComponentRangeSplitPlanStatus::ResultLimitExceeded;
            analysis.error_code = "component_range_split_result_limit_exceeded";
            analysis.error_message = "本次拆分生成的病害数量超过上限。";
            analysis.rejected_target = target;
            return analysis;
        }

        work.summary.result_photo_count =
            source_photo_count * work.summary.expanded_component_count;
        for (const auto& number : expansion.numbers) {
            auto match = analyze_match(number, target.part_name, revision);
            if (!match.bridge_component_id.empty()) {
                work.summary.bound_count += work.summary.source_defect_count;
            } else if (!match.candidate_component_ids.empty()) {
                work.summary.ambiguous_count += work.summary.source_defect_count;
            } else {
                work.summary.unmatched_count += work.summary.source_defect_count;
            }
            work.matches.push_back(std::move(match));
        }

        analysis.totals.source_defect_count += work.summary.source_defect_count;
        analysis.totals.result_defect_count += work.summary.result_defect_count;
        analysis.totals.result_photo_count += work.summary.result_photo_count;
        analysis.totals.bound_count += work.summary.bound_count;
        analysis.totals.ambiguous_count += work.summary.ambiguous_count;
        analysis.totals.unmatched_count += work.summary.unmatched_count;
        analysis.items.push_back(work.summary);
        analysis.work_items.push_back(std::move(work));
    }
    analysis.totals.selected_range_count = static_cast<int>(analysis.items.size());
    return analysis;
}

ComponentRangeSplitPlan materialize_component_range_splits(
    const Json::Value& current,
    const ComponentRangeSplitAnalysis& analysis) {
    auto plan = plan_from_analysis(analysis);
    plan.result_json = current;
    if (analysis.status != ComponentRangeSplitPlanStatus::Ok) return plan;

    std::set<std::string> occupied_ids;
    std::unordered_map<std::string, const Json::Value*> defects_by_id;
    for (const auto& defect : current["defects"]) {
        const auto id = string_member(defect, "candidate_id");
        occupied_ids.insert(id);
        defects_by_id.emplace(id, &defect);
    }
    std::unordered_map<std::string, std::vector<const Json::Value*>> photos_by_defect;
    if (current["photos"].isArray()) {
        for (const auto& photo : current["photos"]) {
            occupied_ids.insert(string_member(photo, "candidate_id"));
            photos_by_defect[string_member(photo, "linked_defect_candidate_id")]
                .push_back(&photo);
        }
    }

    Json::Value output_defects(Json::arrayValue);
    Json::Value output_photos(Json::arrayValue);
    Json::Value output_warnings(Json::arrayValue);
    Json::Value output_errors(Json::arrayValue);
    std::set<std::string> selected_source_ids;

    for (const auto& work : analysis.work_items) {
        for (const auto& source_id : work.source_candidate_ids) {
            const auto source_it = defects_by_id.find(source_id);
            if (source_it == defects_by_id.end()) continue;
            const auto& source = *source_it->second;
            selected_source_ids.insert(source_id);
            for (std::size_t index = 0; index < work.matches.size(); ++index) {
                const auto& match = work.matches[index];
                auto split = source;
                const auto new_id = unique_id(
                    source_id + "__range_" + std::to_string(index + 1), occupied_ids);
                split["candidate_id"] = new_id;
                split["component_number"] = match.component_number;
                split["review_status"] = "待确认";
                split["group_review_status"] = "待确认";
                clear_binding(split);
                split["range_split_origin"] = split_origin(
                    source_id, work.summary.target.component_number,
                    match.component_number, static_cast<int>(index + 1),
                    static_cast<int>(work.matches.size()));
                if (split["warnings"].isArray()) {
                    for (auto& warning : split["warnings"]) {
                        if (string_member(warning, "target_candidate_id") == source_id) {
                            warning["target_candidate_id"] = new_id;
                        }
                    }
                }
                append_split_warning(split["warnings"], new_id);
                apply_analyzed_match(split, match, analysis.inventory_revision_id);

                std::unordered_map<std::string, std::string> split_photo_ids;
                for (const auto* source_photo : photos_by_defect[source_id]) {
                    auto photo = *source_photo;
                    const auto source_photo_id =
                        string_member(*source_photo, "candidate_id");
                    const auto photo_id = unique_id(
                        source_photo_id + "__range_" + std::to_string(index + 1),
                        occupied_ids);
                    photo["candidate_id"] = photo_id;
                    photo["linked_defect_candidate_id"] = new_id;
                    split_photo_ids.emplace(source_photo_id, photo_id);
                    if (photo["warnings"].isArray()) {
                        for (auto& warning : photo["warnings"]) {
                            if (string_member(warning, "target_candidate_id")
                                == source_photo_id) {
                                warning["target_candidate_id"] = photo_id;
                            } else if (string_member(warning, "target_candidate_id")
                                       == source_id) {
                                warning["target_candidate_id"] = new_id;
                            }
                        }
                    }
                    output_photos.append(std::move(photo));
                }
                if (split["photo_references"].isArray()) {
                    for (auto& reference : split["photo_references"]) {
                        const auto photo_id =
                            string_member(reference, "photo_candidate_id");
                        if (const auto found = split_photo_ids.find(photo_id);
                            found != split_photo_ids.end()) {
                            reference["photo_candidate_id"] = found->second;
                        }
                        if (string_member(reference, "resolved_defect_candidate_id")
                            == source_id) {
                            reference["resolved_defect_candidate_id"] = new_id;
                        }
                    }
                }
                output_defects.append(std::move(split));
                if (current["warnings"].isArray()) {
                    for (const auto& warning : current["warnings"]) {
                        rewrite_targeted_warning(warning, source_id, new_id, output_warnings);
                    }
                }
                if (current["errors"].isArray()) {
                    for (const auto& error : current["errors"]) {
                        rewrite_targeted_warning(error, source_id, new_id, output_errors);
                    }
                }
            }
        }
    }

    for (const auto& defect : current["defects"]) {
        if (!selected_source_ids.contains(string_member(defect, "candidate_id"))) {
            output_defects.append(defect);
        }
    }
    if (current["photos"].isArray()) {
        for (const auto& photo : current["photos"]) {
            if (!selected_source_ids.contains(
                    string_member(photo, "linked_defect_candidate_id"))) {
                output_photos.append(photo);
            }
        }
    }
    if (current["warnings"].isArray()) {
        for (const auto& warning : current["warnings"]) {
            if (!selected_source_ids.contains(
                    string_member(warning, "target_candidate_id"))) {
                output_warnings.append(warning);
            }
        }
    }
    if (current["errors"].isArray()) {
        for (const auto& error : current["errors"]) {
            if (!selected_source_ids.contains(
                    string_member(error, "target_candidate_id"))) {
                output_errors.append(error);
            }
        }
    }
    plan.result_json["defects"] = std::move(output_defects);
    if (current.isMember("photos")) {
        plan.result_json["photos"] = std::move(output_photos);
    }
    if (current.isMember("warnings")) {
        plan.result_json["warnings"] = std::move(output_warnings);
    }
    if (current.isMember("errors")) {
        plan.result_json["errors"] = std::move(output_errors);
    }
    return plan;
}

ComponentRangeSplitPlan plan_component_range_splits(
    const Json::Value& current,
    const inventory::InventoryRevision& revision,
    std::vector<ComponentRangeSplitTarget> targets,
    std::size_t max_range_count,
    std::size_t max_result_defects) {
    return materialize_component_range_splits(
        current,
        analyze_component_range_splits(
            current, revision, std::move(targets),
            max_range_count, max_result_defects));
}

}  // namespace bridge_report::review
