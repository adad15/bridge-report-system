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
    return object.isObject() && object[name].isString() ? object[name].asString() : std::string();
}

std::string target_key(const ComponentRangeSplitTarget& target) {
    return target.part_name + "\n"
        + inventory::normalize_component_number(target.component_number);
}

bool is_ineligible(const Json::Value& defect) {
    if (!string_member(defect, "bridge_component_id").empty()) return true;
    return string_member(defect, "component_match_method") == "missing";
}

std::string unique_id(
    const std::string& desired, std::set<std::string>& occupied) {
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

void apply_match(
    Json::Value& defect,
    const inventory::InventoryRevision& revision,
    ComponentRangeSplitItem& item) {
    const inventory::DefectComponentText text{
        string_member(defect, "component_number"),
        string_member(defect, "component_name")};
    const auto match = inventory::match_defect_component(text, revision, {});
    defect["component_inventory_revision_id"] = revision.id;
    defect["component_match_candidate_ids"] = Json::Value(Json::arrayValue);
    for (const auto& id : match.candidate_component_ids) {
        defect["component_match_candidate_ids"].append(id);
    }
    if (match.matched_entry && match.matched_mapping) {
        defect["bridge_component_id"] = match.matched_entry->bridge_component_id;
        defect["standard_component_category_id"] =
            match.matched_mapping->standard_component_category_id;
        const auto& part = match.matched_mapping->structure_part;
        defect["resolved_structure_part"] =
            part == "superstructure" ? "上部结构"
            : part == "substructure" ? "下部结构"
            : part == "deck_system" ? "桥面系"
            : part == "overall" ? "全桥" : "其他";
        defect["component_match_method"] =
            inventory::component_match_method_name(match.method);
        ++item.bound_count;
    } else if (!match.candidate_component_ids.empty()) {
        defect["component_match_method"] =
            inventory::component_match_method_name(match.method);
        ++item.ambiguous_count;
    } else {
        ++item.unmatched_count;
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
    Json::Value warning, const std::string& old_id, const std::string& new_id,
    Json::Value& output) {
    if (string_member(warning, "target_candidate_id") != old_id) return;
    warning["target_candidate_id"] = new_id;
    output.append(std::move(warning));
}

}  // namespace

ComponentRangeSplitPlan plan_component_range_splits(
    const Json::Value& current,
    const inventory::InventoryRevision& revision,
    std::vector<ComponentRangeSplitTarget> targets,
    std::size_t max_range_count,
    std::size_t max_result_defects) {
    ComponentRangeSplitPlan plan;
    plan.result_json = current;
    if (targets.empty() || !current["defects"].isArray()) {
        plan.status = ComponentRangeSplitPlanStatus::InvalidTarget;
        plan.error_code = "component_range_split_invalid_target";
        plan.error_message = "至少选择一个可拆分的构件范围。";
        return plan;
    }

    std::sort(targets.begin(), targets.end(), [](const auto& left, const auto& right) {
        return std::tie(left.part_name, left.component_number)
            < std::tie(right.part_name, right.component_number);
    });
    targets.erase(std::unique(targets.begin(), targets.end(), [](const auto& a, const auto& b) {
        return target_key(a) == target_key(b);
    }), targets.end());

    std::set<std::string> occupied_ids;
    for (const auto& defect : current["defects"]) {
        occupied_ids.insert(string_member(defect, "candidate_id"));
    }
    if (current["photos"].isArray()) {
        for (const auto& photo : current["photos"]) {
            occupied_ids.insert(string_member(photo, "candidate_id"));
        }
    }

    Json::Value output_defects(Json::arrayValue);
    Json::Value output_photos(Json::arrayValue);
    Json::Value output_warnings(Json::arrayValue);
    Json::Value output_errors(Json::arrayValue);
    std::set<std::string> selected_source_ids;

    for (const auto& target : targets) {
        ComponentRangeSplitItem item;
        item.target = target;
        const auto expansion =
            inventory::parse_component_range(target.component_number, max_range_count);
        if (expansion.status != inventory::ComponentRangeParseStatus::Ok) {
            plan.status = expansion.status == inventory::ComponentRangeParseStatus::LimitExceeded
                ? ComponentRangeSplitPlanStatus::RangeLimitExceeded
                : ComponentRangeSplitPlanStatus::InvalidTarget;
            plan.error_code = expansion.status
                    == inventory::ComponentRangeParseStatus::LimitExceeded
                ? "component_range_split_range_limit_exceeded"
                : "component_range_split_invalid_range";
            plan.error_message = expansion.message;
            plan.rejected_target = target;
            return plan;
        }
        item.expanded_component_count = static_cast<int>(expansion.numbers.size());

        std::vector<Json::Value> source_defects;
        for (const auto& defect : current["defects"]) {
            if (string_member(defect, "component_name") == target.part_name
                && inventory::normalize_component_number(
                       string_member(defect, "component_number"))
                    == inventory::normalize_component_number(target.component_number)) {
                if (is_ineligible(defect)) {
                    plan.status = ComponentRangeSplitPlanStatus::IneligibleTarget;
                    plan.error_code = "component_range_split_ineligible";
                    plan.error_message = "已绑定或已标记缺失的构件需先清除处理结果。";
                    plan.rejected_target = target;
                    return plan;
                }
                source_defects.push_back(defect);
            }
        }
        if (source_defects.empty()) {
            plan.status = ComponentRangeSplitPlanStatus::InvalidTarget;
            plan.error_code = "component_range_split_target_not_found";
            plan.error_message = "所选构件范围已不存在。";
            plan.rejected_target = target;
            return plan;
        }
        item.source_defect_count = static_cast<int>(source_defects.size());
        item.result_defect_count =
            item.source_defect_count * item.expanded_component_count;
        if (plan.totals.result_defect_count + item.result_defect_count
            > static_cast<int>(max_result_defects)) {
            plan.status = ComponentRangeSplitPlanStatus::ResultLimitExceeded;
            plan.error_code = "component_range_split_result_limit_exceeded";
            plan.error_message = "本次拆分生成的病害数量超过上限。";
            plan.rejected_target = target;
            return plan;
        }

        for (const auto& source : source_defects) {
            const auto source_id = string_member(source, "candidate_id");
            selected_source_ids.insert(source_id);
            for (std::size_t index = 0; index < expansion.numbers.size(); ++index) {
                auto split = source;
                const auto new_id = unique_id(
                    source_id + "__range_" + std::to_string(index + 1), occupied_ids);
                split["candidate_id"] = new_id;
                split["component_number"] = expansion.numbers[index];
                split["review_status"] = "待确认";
                split["group_review_status"] = "待确认";
                clear_binding(split);
                split["range_split_origin"] = split_origin(
                    source_id, target.component_number, expansion.numbers[index],
                    static_cast<int>(index + 1),
                    static_cast<int>(expansion.numbers.size()));
                if (split["warnings"].isArray()) {
                    for (auto& warning : split["warnings"]) {
                        if (string_member(warning, "target_candidate_id") == source_id) {
                            warning["target_candidate_id"] = new_id;
                        }
                    }
                }
                append_split_warning(split["warnings"], new_id);
                apply_match(split, revision, item);
                output_defects.append(split);

                if (current["photos"].isArray()) {
                    for (const auto& source_photo : current["photos"]) {
                        if (string_member(source_photo, "linked_defect_candidate_id") != source_id)
                            continue;
                        auto photo = source_photo;
                        const auto photo_id = unique_id(
                            string_member(source_photo, "candidate_id") + "__range_"
                                + std::to_string(index + 1),
                            occupied_ids);
                        photo["candidate_id"] = photo_id;
                        photo["linked_defect_candidate_id"] = new_id;
                        if (photo["warnings"].isArray()) {
                            for (auto& warning : photo["warnings"]) {
                                if (string_member(warning, "target_candidate_id")
                                    == string_member(source_photo, "candidate_id")) {
                                    warning["target_candidate_id"] = photo_id;
                                } else if (string_member(warning, "target_candidate_id")
                                           == source_id) {
                                    warning["target_candidate_id"] = new_id;
                                }
                            }
                        }
                        output_photos.append(std::move(photo));
                        ++item.result_photo_count;
                    }
                }
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
        plan.totals.source_defect_count += item.source_defect_count;
        plan.totals.result_defect_count += item.result_defect_count;
        plan.totals.result_photo_count += item.result_photo_count;
        plan.totals.bound_count += item.bound_count;
        plan.totals.ambiguous_count += item.ambiguous_count;
        plan.totals.unmatched_count += item.unmatched_count;
        plan.items.push_back(std::move(item));
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
            if (!selected_source_ids.contains(string_member(warning, "target_candidate_id"))) {
                output_warnings.append(warning);
            }
        }
    }
    if (current["errors"].isArray()) {
        for (const auto& error : current["errors"]) {
            if (!selected_source_ids.contains(string_member(error, "target_candidate_id"))) {
                output_errors.append(error);
            }
        }
    }
    plan.result_json["defects"] = std::move(output_defects);
    if (current.isMember("photos")) plan.result_json["photos"] = std::move(output_photos);
    if (current.isMember("warnings")) plan.result_json["warnings"] = std::move(output_warnings);
    if (current.isMember("errors")) plan.result_json["errors"] = std::move(output_errors);
    plan.totals.selected_range_count = static_cast<int>(plan.items.size());
    return plan;
}

}  // namespace bridge_report::review
