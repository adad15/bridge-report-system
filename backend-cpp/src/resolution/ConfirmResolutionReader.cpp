#include "bridge_report/resolution/ConfirmResolutionReader.hpp"

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <json/json.h>

#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/db/ImportResolutionRepository.hpp"
#include "bridge_report/resolution/EffectiveDefectFacts.hpp"

namespace bridge_report::resolution {
namespace {

std::string contract_structure_part(const std::string& value) {
    if (value == "superstructure") return "上部结构";
    if (value == "substructure") return "下部结构";
    if (value == "deck_system") return "桥面系";
    if (value == "overall") return "全桥";
    return "其他";
}

std::string string_member_or_empty(const Json::Value& object, const char* key) {
    return object.isObject() && object[key].isString() ? object[key].asString()
                                                       : std::string{};
}

Json::Value build_view(
    const drogon::orm::DbClientPtr& client,
    const std::string& import_record_id,
    const Json::Value& parsed_result) {
    Json::Value view = parsed_result;
    view["defects"] = Json::Value(Json::arrayValue);

    std::unordered_map<std::string, const Json::Value*> source_by_candidate;
    if (parsed_result["defects"].isArray()) {
        for (const auto& defect : parsed_result["defects"]) {
            if (defect["candidate_id"].isString()) {
                source_by_candidate.emplace(defect["candidate_id"].asString(), &defect);
            }
        }
    }

    const db::ImportResolutionRepository repository(client);
    const auto groups = repository.list_groups(import_record_id);
    const auto members = repository.list_members(import_record_id);
    const auto instances = repository.list_instances_by_import(import_record_id);
    const auto ratings = repository.list_rating_resolutions_by_import(import_record_id);
    const auto targets = repository.list_targets_by_import(import_record_id);

    std::unordered_map<std::string, const ComponentResolutionGroup*> group_by_id;
    for (const auto& group : groups) group_by_id.emplace(group.id, &group);
    std::unordered_map<std::string, const ComponentGroupMember*> member_by_id;
    for (const auto& member : members) member_by_id.emplace(member.id, &member);
    std::unordered_map<std::string, const ComponentResolutionTarget*> target_by_id;
    for (const auto& target : targets) target_by_id.emplace(target.id, &target);
    std::unordered_map<std::string, const RatingResolution*> rating_by_instance;
    for (const auto& rating : ratings) {
        rating_by_instance.emplace(rating.resolved_defect_instance_id, &rating);
    }

    // 一条来源病害展开出的活动实例数与序号，合成区间溯源要用（§17.2）。
    std::map<std::string, std::vector<const ResolvedDefectInstance*>> active_by_member;
    for (const auto& instance : instances) {
        if (instance.instance_status != "active") continue;
        active_by_member[instance.group_member_id].push_back(&instance);
    }

    // 台账条目按版本缓存：同一份导入里的组通常钉在同一个版本上。
    std::map<std::string, std::optional<inventory::InventoryRevision>> revisions;
    const auto revision_for = [&](const std::optional<std::string>& revision_id,
                                  const std::string& bridge_id)
        -> const std::optional<inventory::InventoryRevision>& {
        static const std::optional<inventory::InventoryRevision> none;
        if (!revision_id.has_value()) return none;
        const auto found = revisions.find(*revision_id);
        if (found != revisions.end()) return found->second;
        auto loaded = db::ComponentInventoryRepository(client).resolve_confirmed_revision(
            bridge_id, revision_id);
        return revisions.emplace(*revision_id, std::move(loaded)).first->second;
    };

    const auto bridge_rows = client->execSqlSync(
        "select bridge_id::text as bridge_id from import_records where id = $1::uuid",
        import_record_id);
    const auto bridge_id = bridge_rows.empty()
        ? std::string{} : bridge_rows[0]["bridge_id"].as<std::string>();

    // 来源病害 -> 持有照片的那条实例。照片链接要跟着改指，否则写计划找不到对应病害，
    // 整批照片会被丢掉。
    std::unordered_map<std::string, std::string> photo_owner_by_source;

    for (const auto& instance : instances) {
        if (instance.instance_status != "active") continue;
        const auto member = member_by_id.find(instance.group_member_id);
        if (member == member_by_id.end()) continue;
        const auto source = source_by_candidate.find(member->second->source_candidate_id);
        if (source == source_by_candidate.end()) continue;
        const auto group = group_by_id.find(member->second->group_id);
        if (group == group_by_id.end()) continue;
        const auto target = target_by_id.find(instance.target_id);

        // 病害事实取有效值：直接给来源值，用户在这条实例上单独调过的东西就入不了库。
        Json::Value defect =
            merge_effective_defect_facts(*source->second, instance.fact_overrides_json);
        defect["candidate_id"] = instance.id;
        defect["source_candidate_id"] = member->second->source_candidate_id;

        if (target != target_by_id.end()) {
            defect["bridge_component_id"] = target->second->bridge_component_id;
            const auto& revision =
                revision_for(group->second->inventory_revision_id, bridge_id);
            if (revision.has_value()) {
                for (const auto& entry : revision->entries) {
                    if (!entry.is_active ||
                        entry.bridge_component_id != target->second->bridge_component_id) {
                        continue;
                    }
                    for (const auto& mapping : entry.mappings) {
                        if (!mapping.is_active || mapping.confirmation_status != "已确认") {
                            continue;
                        }
                        defect["standard_component_category_id"] =
                            mapping.standard_component_category_id;
                        defect["resolved_structure_part"] =
                            contract_structure_part(mapping.structure_part);
                        break;
                    }
                    // 构件类型与编号按台账条目派生，不再沿用草稿里冻结的那份。
                    defect["component_name"] = entry.site_component_type;
                    defect["component_number"] = entry.component_number;
                    break;
                }
            }
        }

        if (const auto rating = rating_by_instance.find(instance.id);
            rating != rating_by_instance.end() && rating->second->status == "matched") {
            defect["rating_tree_node_id"] =
                rating->second->rating_tree_node_id.value_or(std::string{});
            defect["rating_tree_version_id"] = rating->second->rating_tree_version_id;
            defect["standard_defect_indicator_id"] =
                rating->second->standard_defect_indicator_id.value_or(std::string{});
        }

        // 照片只跟持有者走。其余实例写空数组——每条都带一份的话，同一个照片编号会出现在
        // N 条观测上，报告里的编号交叉引用就作废了。
        if (instance.is_photo_owner) {
            photo_owner_by_source.emplace(member->second->source_candidate_id, instance.id);
        } else {
            defect["photo_references"] = Json::Value(Json::arrayValue);
        }

        // 区间溯源：仅在展开为多个活动实例时合成，与"单构件绑定不带溯源"一致。
        const auto& siblings = active_by_member[instance.group_member_id];
        if (siblings.size() > 1) {
            int split_index = 1;
            for (std::size_t position = 0; position < siblings.size(); ++position) {
                if (siblings[position]->id == instance.id) {
                    split_index = static_cast<int>(position) + 1;
                    break;
                }
            }
            Json::Value origin(Json::objectValue);
            origin["operation_id"] = group->second->id;
            origin["source_candidate_id"] = member->second->source_candidate_id;
            origin["source_component_number"] =
                group->second->source_component_number.value_or(
                    group->second->normalized_component_number);
            origin["expanded_component_number"] =
                string_member_or_empty(defect, "component_number");
            origin["split_index"] = split_index;
            origin["split_count"] = static_cast<int>(siblings.size());
            // 多实例只可能由人工操作产生；遇空是内部错误，留空让预检把它挡下来，
            // 而不是塞一个占位值进正式事实。
            origin["operated_by_user_id"] =
                group->second->resolved_by_user_id.value_or(std::string{});
            origin["operated_at"] = group->second->resolved_at.value_or(std::string{});
            defect["range_split_origin"] = std::move(origin);
        }

        view["defects"].append(std::move(defect));
    }

    if (view["photos"].isArray()) {
        for (auto& photo : view["photos"]) {
            const auto linked = string_member_or_empty(photo, "linked_defect_candidate_id");
            if (linked.empty()) continue;
            const auto owner = photo_owner_by_source.find(linked);
            photo["linked_defect_candidate_id"] = owner == photo_owner_by_source.end()
                ? Json::Value() : Json::Value(owner->second);
        }
    }

    // 预检要能回答"这条来源病害到底解析了没有"。视图里只有解析成功的实例，光看它
    // 分不出"已标记缺失"（合法、不入库）和"还没解析"（必须阻断）——两者都是没有实例。
    Json::Value summary(Json::objectValue);
    summary["resolved_source_candidate_ids"] = Json::Value(Json::arrayValue);
    summary["missing_source_candidate_ids"] = Json::Value(Json::arrayValue);
    std::set<std::string> resolved_sources;
    for (const auto& defect : view["defects"]) {
        if (!string_member_or_empty(defect, "bridge_component_id").empty()) {
            resolved_sources.insert(string_member_or_empty(defect, "source_candidate_id"));
        }
    }
    for (const auto& source_id : resolved_sources) {
        summary["resolved_source_candidate_ids"].append(source_id);
    }
    for (const auto& member : members) {
        const auto group = group_by_id.find(member.group_id);
        if (group == group_by_id.end() || group->second->status != "missing") continue;
        summary["missing_source_candidate_ids"].append(member.source_candidate_id);
    }
    view["resolution_summary"] = std::move(summary);

    return view;
}

}  // namespace

Json::Value build_confirmable_view(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const std::string& import_record_id,
    const Json::Value& parsed_result) {
    return build_view(tx, import_record_id, parsed_result);
}

Json::Value build_confirmable_view(
    const drogon::orm::DbClientPtr& client,
    const std::string& import_record_id,
    const Json::Value& parsed_result) {
    return build_view(client, import_record_id, parsed_result);
}

}  // namespace bridge_report::resolution
