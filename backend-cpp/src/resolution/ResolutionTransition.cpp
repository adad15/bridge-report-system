#include "bridge_report/resolution/ResolutionTransition.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <utility>

#include <json/json.h>

#include "bridge_report/db/ImportResolutionRepository.hpp"
#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/rating_tree/RatingTreeResolver.hpp"
#include "bridge_report/resolution/EffectiveDefectFacts.hpp"
#include "bridge_report/resolution/ResolutionHashes.hpp"

namespace bridge_report::resolution {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::string string_member_or_empty(const Json::Value& object, const char* key) {
    return object.isObject() && object[key].isString() ? object[key].asString()
                                                       : std::string{};
}

std::unordered_map<std::string, const Json::Value*> index_defects(
    const Json::Value& parsed) {
    std::unordered_map<std::string, const Json::Value*> by_candidate;
    if (!parsed["defects"].isArray()) return by_candidate;
    for (const auto& defect : parsed["defects"]) {
        if (defect["candidate_id"].isString()) {
            by_candidate.emplace(defect["candidate_id"].asString(), &defect);
        }
    }
    return by_candidate;
}

const inventory::InventoryMapping* find_active_mapping(
    const inventory::InventoryRevision& revision,
    const std::string& bridge_component_id,
    const std::string& technical_standard_package_id) {
    for (const auto& entry : revision.entries) {
        if (!entry.is_active || entry.bridge_component_id != bridge_component_id) continue;
        for (const auto& mapping : entry.mappings) {
            if (mapping.is_active && mapping.confirmation_status == "已确认" &&
                mapping.standard_package_id == technical_standard_package_id) {
                return &mapping;
            }
        }
    }
    return nullptr;
}

Json::Value match_evidence_document(const rating_tree::RatingTreeMatchResult& result) {
    Json::Value evidence(Json::objectValue);
    evidence["outcome"] = rating_tree::to_string(result.outcome);
    evidence["reason_code"] = result.reason_code;
    evidence["reason_message"] = result.reason_message;
    if (!result.match_evidence.empty()) evidence["evidence"] = result.match_evidence;
    return evidence;
}

}  // namespace

std::optional<RatingContext> load_rating_context(
    const TransactionPtr& tx, const std::string& inspection_year_id) {
    if (inspection_year_id.empty()) return std::nullopt;
    const auto profile = tx->execSqlSync(
        "select psp.rating_tree_version_id::text as rating_tree_version_id,"
        "psp.technical_condition_package_id::text as technical_package_id "
        "from inspection_years iy "
        "join project_standard_profiles psp on psp.id = iy.standard_profile_id "
        "where iy.id = $1::uuid",
        inspection_year_id);
    if (profile.empty() || profile[0]["rating_tree_version_id"].isNull() ||
        profile[0]["technical_package_id"].isNull()) {
        return std::nullopt;
    }
    RatingContext context;
    context.rating_tree_version_id =
        profile[0]["rating_tree_version_id"].as<std::string>();
    context.technical_standard_package_id =
        profile[0]["technical_package_id"].as<std::string>();
    auto tree =
        db::RatingTreeRepository(tx).load_published_tree(context.rating_tree_version_id);
    if (!tree.has_value()) return std::nullopt;
    context.tree = std::move(*tree);
    return context;
}

void refresh_rating_resolution(
    const TransactionPtr& tx,
    const ResolvedDefectInstance& instance,
    const Json::Value& source_defect,
    const std::string& bridge_component_id,
    const inventory::InventoryRevision& revision,
    const RatingContext& rating,
    const int component_resolution_version,
    int& rewritten,
    int& preserved_manual) {
    const db::ImportResolutionRepository repository(tx);

    const auto* mapping = find_active_mapping(
        revision, bridge_component_id, rating.technical_standard_package_id);
    if (mapping == nullptr) {
        // 目标构件在当前规范包下没有已确认映射：解析不出适用范围，旧结果一律作废。
        repository.delete_rating_resolution(instance.id);
        return;
    }

    // 哈希按**有效事实**算：三个实例把同一条来源病害改成了不同类型时，它们本就该
    // 各自重算；按来源值算会让三条哈希恒等，覆盖再怎么改也触发不了失效。
    const auto effective =
        merge_effective_defect_facts(source_defect, instance.fact_overrides_json);
    const auto hashes = compute_rating_match_hashes(build_rating_match_hash_input(
        effective,
        string_member_or_empty(source_defect, "candidate_id"),
        bridge_component_id,
        rating.technical_standard_package_id,
        mapping->standard_bridge_type_id,
        mapping->standard_component_category_id,
        rating.rating_tree_version_id));

    const auto existing = repository.find_rating_resolution(instance.id);
    const bool is_manual =
        existing.has_value() && existing->match_method.has_value() &&
        *existing->match_method == "manual" && existing->status == "matched";

    if (is_manual && existing->applicability_hash == hashes.applicability_hash) {
        // 人工选择只有用户自己能改：文字、位置、描述变了也保留节点，只更新哈希并
        // 让读模型给出复核提示。把它按自动结果冲掉是行为变更，不能借哈希偷偷实现。
        RatingResolution kept = *existing;
        kept.match_input_hash = hashes.match_input_hash;
        kept.component_resolution_version = component_resolution_version;
        repository.upsert_rating_resolution(kept);
        ++preserved_manual;
        return;
    }

    const rating_tree::RatingTreeResolver resolver;
    rating_tree::RatingTreeMatchInput input;
    input.bridge_type_id = mapping->standard_bridge_type_id;
    input.component_category_id = mapping->standard_component_category_id;
    input.defect_type = string_member_or_empty(effective, "defect_type");
    input.defect_description = string_member_or_empty(effective, "defect_description");
    input.defect_location = string_member_or_empty(effective, "defect_location");
    input.source_defect_group_id =
        string_member_or_empty(effective, "source_defect_group_id");
    input.source_defect_group_number =
        string_member_or_empty(effective, "source_defect_group_number");
    input.source_defect_indicator_id =
        string_member_or_empty(effective, "source_defect_indicator_id");
    input.source_defect_indicator_number =
        string_member_or_empty(effective, "source_defect_indicator_number");
    const auto match = resolver.resolve(rating.tree, input);

    RatingResolution resolution;
    resolution.resolved_defect_instance_id = instance.id;
    resolution.rating_tree_version_id = rating.rating_tree_version_id;
    resolution.component_resolution_version = component_resolution_version;
    resolution.applicability_hash = hashes.applicability_hash;
    resolution.match_input_hash = hashes.match_input_hash;
    resolution.match_evidence_json = match_evidence_document(match);
    if (match.outcome == rating_tree::RatingTreeMatchOutcome::auto_bound &&
        match.node_id.has_value()) {
        resolution.status = "matched";
        resolution.rating_tree_node_id = match.node_id;
        resolution.standard_defect_indicator_id = match.h21_indicator_id;
        resolution.match_method = match.match_method.empty()
            ? std::optional<std::string>("exact")
            : std::optional<std::string>(match.match_method);
    } else {
        resolution.status = "unresolved";
    }
    repository.upsert_rating_resolution(resolution);
    ++rewritten;
}

ResolutionTransitionResult apply_component_resolution_transition(
    const TransactionPtr& tx,
    const std::string& group_id,
    const int expected_version,
    const ResolutionTransitionInput& input,
    const Json::Value& parsed_result,
    const std::optional<inventory::InventoryRevision>& revision,
    const std::optional<RatingContext>& rating) {
    ResolutionTransitionResult result;
    const db::ImportResolutionRepository repository(tx);

    // 步骤 1：版本条件写。对不上就一行都不动，由调用方翻成版本冲突。
    const auto new_version = repository.update_group_resolution(
        group_id,
        expected_version,
        input.status,
        input.match_method,
        input.inventory_revision_id,
        input.resolution_mode,
        input.actor_user_id);
    if (!new_version.has_value()) {
        result.error_code = "resolution_version_conflict";
        result.error_message = "构件解析版本已过期，请刷新后重试。";
        return result;
    }
    result.group_version = *new_version;

    const auto before_targets = repository.list_targets(group_id);
    const auto members = repository.list_members(input.import_record_id);

    // 步骤 2：原子替换目标。旧目标全删再按新集合重建，级联会带走挂在旧目标上的实例，
    // **连同它们的评分树解析行**。差量对齐在步骤 3 里补回来，所以这里必须把实例和
    // 评分树解析一起先抄下来——删完再去查是查不到的，人工选的节点会就此消失。
    using InstanceKey = std::pair<std::string, std::string>;  // (member_id, 构件 id)
    std::map<InstanceKey, ResolvedDefectInstance> kept_by_key;
    std::map<InstanceKey, RatingResolution> kept_rating_by_key;
    for (const auto& member : members) {
        if (member.group_id != group_id) continue;
        for (const auto& instance : repository.list_instances_by_member(member.id)) {
            for (const auto& target : before_targets) {
                if (target.id != instance.target_id) continue;
                const InstanceKey key{member.id, target.bridge_component_id};
                kept_by_key.emplace(key, instance);
                if (const auto rating_row = repository.find_rating_resolution(instance.id);
                    rating_row.has_value()) {
                    kept_rating_by_key.emplace(key, *rating_row);
                }
                break;
            }
        }
    }

    repository.delete_targets(group_id);
    std::unordered_map<std::string, std::string> target_id_by_component;
    int target_order = 1;
    for (const auto& selection : input.targets) {
        ComponentResolutionTarget target;
        target.group_id = group_id;
        target.bridge_component_id = selection.bridge_component_id;
        target.target_order = target_order++;
        target.target_role = selection.target_role;
        target_id_by_component.emplace(
            selection.bridge_component_id, repository.insert_target(target).id);
    }

    // 步骤 3：按 (group_member_id, bridge_component_id) 差量对齐。
    //
    // 键用构件而不是 target_id：目标行在步骤 2 里已经被重建，id 必然是新的，按 id 比
    // 永远命不中，等同于整组重建——三目标里换掉一个，另外两条上人工逐条调过的位置、
    // 尺寸、标度和"这条不要"的判断会一起没掉，而用户的操作意图只是换第三个目标。
    const auto defects_by_candidate = index_defects(parsed_result);
    for (const auto& member : members) {
        if (member.group_id != group_id) continue;
        int instance_order = 1;
        for (const auto& selection : input.targets) {
            const auto target_id = target_id_by_component.find(selection.bridge_component_id);
            if (target_id == target_id_by_component.end()) continue;

            const InstanceKey key{member.id, selection.bridge_component_id};
            const auto kept = kept_by_key.find(key);
            ResolvedDefectInstance instance;
            instance.group_member_id = member.id;
            instance.target_id = target_id->second;
            instance.instance_order = instance_order++;
            instance.component_resolution_version = result.group_version;
            if (kept != kept_by_key.end()) {
                // 保留意味着覆盖与忽略状态跟着这一行活下来。
                instance.instance_status = kept->second.instance_status;
                instance.fact_overrides_json = kept->second.fact_overrides_json;
                ++result.instances_kept;
            } else {
                ++result.instances_created;
            }
            const auto stored = repository.insert_instance(instance);

            // 步骤 5–7：按有效事实重算哈希，人工与自动分别处理失效。
            if (rating.has_value() && revision.has_value() &&
                stored.instance_status == "active") {
                const auto defect = defects_by_candidate.find(member.source_candidate_id);
                if (defect != defects_by_candidate.end()) {
                    // 先把抄下来的旧解析行搬到新实例 id 上，refresh 才认得出这是不是
                    // 人工选的节点——认不出就会当成自动结果直接冲掉。
                    if (const auto previous = kept_rating_by_key.find(key);
                        previous != kept_rating_by_key.end()) {
                        RatingResolution moved = previous->second;
                        moved.resolved_defect_instance_id = stored.id;
                        repository.upsert_rating_resolution(moved);
                    }
                    refresh_rating_resolution(
                        tx, stored, *defect->second, selection.bridge_component_id,
                        *revision, *rating, result.group_version,
                        result.rating_rewritten, result.rating_preserved_manual);
                }
            }
        }
        // 步骤 8：照片归属按活动实例中序号最小者重算。
        repository.recompute_photo_owner(member.id);
    }
    result.instances_deleted =
        static_cast<int>(kept_by_key.size()) - result.instances_kept;

    // 步骤 9：审计。当前状态表只表达当前结果，历史只在事件表里。
    ResolutionEvent event;
    event.import_record_id = input.import_record_id;
    event.group_id = group_id;
    event.plan_id = input.plan_id;
    event.operation_type =
        input.operation_type.empty() ? "component_resolution" : input.operation_type;
    event.actor_user_id = input.actor_user_id;
    Json::Value before(Json::objectValue);
    before["version"] = expected_version;
    before["target_count"] = static_cast<int>(before_targets.size());
    Json::Value after(Json::objectValue);
    after["version"] = result.group_version;
    after["status"] = input.status;
    after["target_count"] = static_cast<int>(input.targets.size());
    after["instances_kept"] = result.instances_kept;
    after["instances_created"] = result.instances_created;
    event.before_json = std::move(before);
    event.after_json = std::move(after);
    repository.append_event(event);

    result.success = true;
    return result;
}

}  // namespace bridge_report::resolution
