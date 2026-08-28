#include "bridge_report/resolution/DraftResolutionSynchronizer.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

#include <json/json.h>

#include "bridge_report/db/ImportResolutionRepository.hpp"
#include "bridge_report/inventory/ComponentMatcher.hpp"
#include "bridge_report/resolution/ResolutionTransition.hpp"

namespace bridge_report::resolution {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::string string_member_or_empty(const Json::Value& object, const char* key) {
    return object.isObject() && object[key].isString() ? object[key].asString()
                                                       : std::string{};
}

std::unordered_map<std::string, const Json::Value*> index_defects(
    const Json::Value& draft) {
    std::unordered_map<std::string, const Json::Value*> by_candidate;
    if (!draft["defects"].isArray()) return by_candidate;
    for (const auto& defect : draft["defects"]) {
        if (defect["candidate_id"].isString()) {
            by_candidate.emplace(defect["candidate_id"].asString(), &defect);
        }
    }
    return by_candidate;
}

/// 影响评分树匹配输入的来源字段。改了它们就要重算；改尺寸、标度、备注则不必。
bool match_input_changed(const Json::Value& before, const Json::Value& after) {
    for (const auto* field : {
             "defect_type", "defect_location", "defect_description",
             "source_defect_group_id", "source_defect_group_number",
             "source_defect_indicator_id", "source_defect_indicator_number"}) {
        if (string_member_or_empty(before, field) != string_member_or_empty(after, field)) {
            return true;
        }
    }
    return false;
}

}  // namespace

DraftSyncResult synchronize_draft_resolution(
    const TransactionPtr& tx,
    const std::string& import_record_id,
    const std::string& bridge_id,
    const std::string& inspection_year_id,
    const Json::Value& stored_draft,
    const Json::Value& new_draft,
    const std::optional<inventory::InventoryRevision>& revision) {
    DraftSyncResult result;
    (void)bridge_id;

    const auto before = index_defects(stored_draft);
    const auto after = index_defects(new_draft);

    const auto members = db::ImportResolutionRepository(tx).list_members(import_record_id);
    std::unordered_map<std::string, const ComponentGroupMember*> member_by_candidate;
    for (const auto& member : members) {
        member_by_candidate.emplace(member.source_candidate_id, &member);
    }
    const auto groups = db::ImportResolutionRepository(tx).list_groups(import_record_id);
    std::unordered_map<std::string, const ComponentResolutionGroup*> group_by_id;
    for (const auto& group : groups) group_by_id.emplace(group.id, &group);

    // 来源构件身份不可改：校对页本来也只读展示这两个值，没有产生这种请求的正常路径。
    // 允许它就得同时处理"搬走后旧组变空、新组可能已绑到别处、两边实例与评分树各自
    // 失效"，代价与使用频率不成比例（§3.8）。
    for (const auto& [candidate_id, defect] : after) {
        const auto previous = before.find(candidate_id);
        if (previous == before.end()) continue;
        if (string_member_or_empty(*previous->second, "component_name") !=
                string_member_or_empty(*defect, "component_name") ||
            string_member_or_empty(*previous->second, "component_number") !=
                string_member_or_empty(*defect, "component_number")) {
            result.error_code = "source_component_identity_immutable";
            result.error_message =
                "来源构件名称与编号不可在普通草稿里修改；请删除该条后重新手工新增。";
            return result;
        }
    }

    // 删除的候选：删成员（级联带走实例与评分树解析），空组随之删除。
    for (const auto& [candidate_id, member] : member_by_candidate) {
        if (after.contains(candidate_id)) continue;
        db::ImportResolutionRepository(tx).delete_member(member->id);
        ++result.members_removed;
    }
    if (result.members_removed > 0) {
        result.groups_removed =
            db::ImportResolutionRepository(tx).delete_empty_groups(import_record_id);
    }

    const auto rating = load_rating_context(tx, inspection_year_id);

    // 新增的候选：建组或复用组并加成员。组已绑定时补出实例并匹配评分树。
    std::map<std::pair<std::string, std::string>, std::string> group_id_by_key;
    for (const auto& group : groups) {
        group_id_by_key.emplace(
            std::make_pair(group.source_component_name, group.normalized_component_number),
            group.id);
    }
    int next_source_order = 0;
    for (const auto& member : members) {
        next_source_order = (std::max)(next_source_order, member.source_order + 1);
    }

    for (const auto& [candidate_id, defect] : after) {
        if (member_by_candidate.contains(candidate_id)) continue;
        const auto part_name = string_member_or_empty(*defect, "component_name");
        const auto raw_number = string_member_or_empty(*defect, "component_number");
        const auto normalized = inventory::normalize_component_number(raw_number);
        const auto key = std::make_pair(part_name, normalized);

        std::string group_id;
        int group_version = 1;
        if (const auto found = group_id_by_key.find(key); found != group_id_by_key.end()) {
            group_id = found->second;
            group_version = group_by_id.at(group_id)->version;
        } else {
            ComponentResolutionGroup group;
            group.import_record_id = import_record_id;
            group.source_component_name = part_name;
            if (!raw_number.empty()) group.source_component_number = raw_number;
            group.normalized_component_number = normalized;
            if (revision.has_value()) group.inventory_revision_id = revision->id;
            const auto stored = db::ImportResolutionRepository(tx).insert_group(group);
            group_id = stored.id;
            group_version = stored.version;
            group_id_by_key.emplace(key, group_id);
        }

        ComponentGroupMember member;
        member.import_record_id = import_record_id;
        member.group_id = group_id;
        member.source_candidate_id = candidate_id;
        member.source_order = next_source_order++;
        const auto stored_member =
            db::ImportResolutionRepository(tx).insert_member(member);
        ++result.members_added;

        // 组已绑定时补出实例：新病害与组内其他病害绑的是同一批构件。
        const auto targets = db::ImportResolutionRepository(tx).list_targets(group_id);
        int instance_order = 1;
        for (const auto& target : targets) {
            ResolvedDefectInstance instance;
            instance.group_member_id = stored_member.id;
            instance.target_id = target.id;
            instance.instance_order = instance_order++;
            instance.component_resolution_version = group_version;
            const auto stored_instance =
                db::ImportResolutionRepository(tx).insert_instance(instance);
            if (rating.has_value() && revision.has_value()) {
                int rewritten = 0;
                int preserved = 0;
                refresh_rating_resolution(
                    tx, stored_instance, *defect, target.bridge_component_id,
                    *revision, *rating, group_version, rewritten, preserved);
                result.ratings_recomputed += rewritten;
            }
        }
        if (!targets.empty()) {
            db::ImportResolutionRepository(tx).recompute_photo_owner(stored_member.id);
        }
    }

    // 修改了匹配输入的候选：按新的有效值重算。改尺寸、数量、标度、备注或照片关系
    // 不走这里——它们不参与评分树节点的选择。
    if (rating.has_value() && revision.has_value()) {
        const auto instances =
            db::ImportResolutionRepository(tx).list_instances_by_import(import_record_id);
        const auto targets =
            db::ImportResolutionRepository(tx).list_targets_by_import(import_record_id);
        std::unordered_map<std::string, std::string> component_by_target;
        for (const auto& target : targets) {
            component_by_target.emplace(target.id, target.bridge_component_id);
        }
        std::unordered_map<std::string, const ComponentGroupMember*> member_by_id;
        for (const auto& member : members) member_by_id.emplace(member.id, &member);

        for (const auto& instance : instances) {
            if (instance.instance_status != "active") continue;
            const auto member = member_by_id.find(instance.group_member_id);
            if (member == member_by_id.end()) continue;
            const auto previous = before.find(member->second->source_candidate_id);
            const auto current = after.find(member->second->source_candidate_id);
            if (previous == before.end() || current == after.end()) continue;
            if (!match_input_changed(*previous->second, *current->second)) continue;
            const auto component = component_by_target.find(instance.target_id);
            if (component == component_by_target.end()) continue;

            const auto group = group_by_id.find(member->second->group_id);
            const int version = group == group_by_id.end() ? 1 : group->second->version;
            int rewritten = 0;
            int preserved = 0;
            refresh_rating_resolution(
                tx, instance, *current->second, component->second, *revision, *rating,
                version, rewritten, preserved);
            result.ratings_recomputed += rewritten;
        }
    }

    result.ok = true;
    return result;
}

}  // namespace bridge_report::resolution
