#include "bridge_report/resolution/ImportResolutionInitializer.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <json/json.h>

#include "bridge_report/db/ImportResolutionRepository.hpp"
#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/inventory/ComponentMatcher.hpp"
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

// 组身份：部件名称原文 + 权威归一化编号。与现网绑定面板的分组口径逐字一致
// （ImportBindingRepository.cpp:78），拆表不改业务语义。
struct GroupKey {
    std::string source_component_name;
    std::string normalized_component_number;

    bool operator<(const GroupKey& other) const {
        if (source_component_name != other.source_component_name) {
            return source_component_name < other.source_component_name;
        }
        return normalized_component_number < other.normalized_component_number;
    }
};

struct PendingGroup {
    GroupKey key;
    // 该组第一条成员的编号原文，只用于展示与审计；判定一律走归一化值。
    std::optional<std::string> source_component_number;
    std::vector<const Json::Value*> defects;
    std::vector<int> source_orders;
};

/// 评分树上下文：年度绑定的评定树版本 + 技术规范包。缺任何一样都不写评分树解析行。
struct RatingContext {
    std::string rating_tree_version_id;
    std::string technical_standard_package_id;
    rating_tree::EffectiveRatingTree tree;
};

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
    auto tree = db::RatingTreeRepository(tx).load_published_tree(
        context.rating_tree_version_id);
    if (!tree.has_value()) return std::nullopt;
    context.tree = std::move(*tree);
    return context;
}

std::vector<inventory::ConfirmedComponentAlias> load_confirmed_aliases(
    const TransactionPtr& tx, const std::string& bridge_id) {
    std::vector<inventory::ConfirmedComponentAlias> aliases;
    const auto rows = tx->execSqlSync(
        "select ca.bridge_component_id::text as bridge_component_id, ca.alias_text "
        "from component_aliases ca "
        "join bridge_components c on c.id = ca.bridge_component_id "
        "where c.bridge_id = $1::uuid and ca.is_manually_confirmed",
        bridge_id);
    for (const auto& row : rows) {
        aliases.push_back({
            row["bridge_component_id"].as<std::string>(),
            row["alias_text"].as<std::string>()});
    }
    return aliases;
}

/// 目标构件在本次台账版本下的规范映射；没有已确认映射时评分树无从解析。
const inventory::InventoryMapping* find_active_mapping(
    const inventory::InventoryRevision& revision,
    const std::string& bridge_component_id,
    const std::string& technical_standard_package_id) {
    for (const auto& entry : revision.entries) {
        if (!entry.is_active || entry.bridge_component_id != bridge_component_id) {
            continue;
        }
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
    if (!result.candidates.empty()) {
        evidence["candidates"] = Json::Value(Json::arrayValue);
        for (const auto& candidate : result.candidates) {
            Json::Value item(Json::objectValue);
            item["node_id"] = candidate.node_id;
            item["display_name"] = candidate.display_name;
            item["match_method"] = candidate.match_method;
            item["evidence"] = candidate.evidence;
            evidence["candidates"].append(std::move(item));
        }
    }
    return evidence;
}

}  // namespace

ImportResolutionInitializationResult initialize_import_resolution(
    const TransactionPtr& tx,
    const ImportResolutionInitializationContext& context,
    const Json::Value& parsed_result) {
    ImportResolutionInitializationResult result;
    const db::ImportResolutionRepository repository(tx);

    try {
        // --- 步骤 2/3：建组与成员 -----------------------------------------
        std::map<GroupKey, PendingGroup> pending;
        if (parsed_result["defects"].isArray()) {
            int source_order = 0;
            for (const auto& defect : parsed_result["defects"]) {
                const auto component_name = string_member_or_empty(defect, "component_name");
                const auto raw_number = string_member_or_empty(defect, "component_number");
                GroupKey key{
                    component_name,
                    inventory::normalize_component_number(raw_number)};
                auto& group = pending[key];
                if (group.defects.empty()) {
                    group.key = key;
                    // 编号原文取该组按来源顺序的第一条；后来的不覆盖它。
                    if (!raw_number.empty()) group.source_component_number = raw_number;
                }
                group.defects.push_back(&defect);
                group.source_orders.push_back(source_order);
                ++source_order;
            }
        }

        const bool has_revision =
            context.revision.has_value() &&
            context.revision->bridge_id == context.bridge_id;
        result.summary.inventory_unconfirmed = !has_revision;

        std::vector<inventory::ConfirmedComponentAlias> aliases;
        if (has_revision) aliases = load_confirmed_aliases(tx, context.bridge_id);

        // 实例 -> (来源病害, 目标构件)，评分树那一轮要用。
        struct PendingInstance {
            std::string instance_id;
            const Json::Value* defect{nullptr};
            std::string source_candidate_id;
            std::string bridge_component_id;
            int component_resolution_version{1};
        };
        std::vector<PendingInstance> instances;

        for (auto& [key, group] : pending) {
            // --- 步骤 5 的构件匹配 ---------------------------------------
            // 没有台账版本时整段跳过：组建出来停在 unresolved，导入照常可校对。
            inventory::ComponentMatchResult match;
            if (has_revision) {
                const inventory::DefectComponentText text{
                    group.source_component_number.value_or(std::string{}),
                    key.source_component_name};
                match = inventory::match_defect_component(text, *context.revision, aliases);
            }
            const bool bound =
                has_revision && match.matched_entry.has_value() &&
                match.matched_mapping.has_value();

            ComponentResolutionGroup row;
            row.import_record_id = context.import_record_id;
            row.source_component_name = key.source_component_name;
            row.source_component_number = group.source_component_number;
            row.normalized_component_number = key.normalized_component_number;
            row.resolution_mode = "single";
            row.status = bound ? "bound" : "unresolved";
            if (bound) {
                row.match_method =
                    inventory::component_match_method_name(match.method);
                row.inventory_revision_id = context.revision->id;
            } else if (has_revision) {
                // 未匹配也钉住版本：它记录的是"这一版台账里没找到"，人工解析时
                // 才知道当时比对的是哪一版。
                row.inventory_revision_id = context.revision->id;
            }
            const auto stored_group = repository.insert_group(row);

            if (bound) {
                ++result.summary.bound_group_count;
            } else {
                ++result.summary.unresolved_group_count;
                if (!match.candidate_component_ids.empty()) {
                    ++result.summary.ambiguous_group_count;
                }
            }
            ++result.summary.group_count;

            std::optional<std::string> target_id;
            if (bound) {
                ComponentResolutionTarget target;
                target.group_id = stored_group.id;
                target.bridge_component_id = match.matched_entry->bridge_component_id;
                target.target_order = 1;
                target.target_role = "primary";
                target_id = repository.insert_target(target).id;
            }

            for (std::size_t index = 0; index < group.defects.size(); ++index) {
                const auto* defect = group.defects[index];
                ComponentGroupMember member;
                member.import_record_id = context.import_record_id;
                member.group_id = stored_group.id;
                member.source_candidate_id =
                    string_member_or_empty(*defect, "candidate_id");
                member.source_order = group.source_orders[index];
                const auto stored_member = repository.insert_member(member);
                ++result.summary.member_count;

                if (!target_id.has_value()) continue;

                ResolvedDefectInstance instance;
                instance.group_member_id = stored_member.id;
                instance.target_id = *target_id;
                instance.instance_order = 1;
                instance.instance_status = "active";
                // 自动匹配只写一个目标，所以这条实例就是唯一的活动实例，
                // 照片归属天然落在它身上。
                instance.is_photo_owner = true;
                instance.component_resolution_version = stored_group.version;
                const auto stored_instance = repository.insert_instance(instance);
                ++result.summary.instance_count;

                instances.push_back(PendingInstance{
                    stored_instance.id,
                    defect,
                    member.source_candidate_id,
                    match.matched_entry->bridge_component_id,
                    stored_group.version});
            }
        }

        // --- 步骤 5 的评分树匹配 ------------------------------------------
        // 这一段单独包 SAVEPOINT。评定树是导入的便利层：目录不可用或某条规则炸了
        // 不该把整批解析结果挡在门外——实例留着没有评分树解析行是合法状态
        // （确认时按现有规则阻断），而 PostgreSQL 里一条语句失败会让整个事务进入
        // aborted 态，光靠 try/catch 救不回来，所以必须有保存点。
        if (!instances.empty()) {
            tx->execSqlSync("savepoint import_resolution_rating_match");
            try {
                const auto rating = load_rating_context(tx, context.inspection_year_id);
                if (rating.has_value()) {
                    const rating_tree::RatingTreeResolver resolver;
                    for (const auto& pending_instance : instances) {
                        const auto* mapping = find_active_mapping(
                            *context.revision,
                            pending_instance.bridge_component_id,
                            rating->technical_standard_package_id);
                        if (mapping == nullptr) continue;

                        // 初始化时还没有任何覆盖，有效事实就是来源事实；仍然走合并
                        // 函数，免得日后加了覆盖来源这里成为唯一漏读的地方。
                        const auto effective = merge_effective_defect_facts(
                            *pending_instance.defect, Json::Value(Json::objectValue));

                        rating_tree::RatingTreeMatchInput input;
                        input.bridge_type_id = mapping->standard_bridge_type_id;
                        input.component_category_id =
                            mapping->standard_component_category_id;
                        input.defect_type = string_member_or_empty(effective, "defect_type");
                        input.defect_description =
                            string_member_or_empty(effective, "defect_description");
                        input.defect_location =
                            string_member_or_empty(effective, "defect_location");
                        input.source_defect_group_id =
                            string_member_or_empty(effective, "source_defect_group_id");
                        input.source_defect_group_number = string_member_or_empty(
                            effective, "source_defect_group_number");
                        input.source_defect_indicator_id = string_member_or_empty(
                            effective, "source_defect_indicator_id");
                        input.source_defect_indicator_number = string_member_or_empty(
                            effective, "source_defect_indicator_number");

                        const auto match = resolver.resolve(rating->tree, input);
                        const auto hashes = compute_rating_match_hashes(
                            build_rating_match_hash_input(
                                effective,
                                pending_instance.source_candidate_id,
                                pending_instance.bridge_component_id,
                                rating->technical_standard_package_id,
                                mapping->standard_bridge_type_id,
                                mapping->standard_component_category_id,
                                rating->rating_tree_version_id));

                        RatingResolution resolution;
                        resolution.resolved_defect_instance_id =
                            pending_instance.instance_id;
                        resolution.rating_tree_version_id =
                            rating->rating_tree_version_id;
                        resolution.component_resolution_version =
                            pending_instance.component_resolution_version;
                        resolution.applicability_hash = hashes.applicability_hash;
                        resolution.match_input_hash = hashes.match_input_hash;
                        resolution.match_evidence_json = match_evidence_document(match);

                        const bool auto_bound =
                            match.outcome ==
                                rating_tree::RatingTreeMatchOutcome::auto_bound &&
                            match.node_id.has_value();
                        if (auto_bound) {
                            resolution.status = "matched";
                            resolution.rating_tree_node_id = match.node_id;
                            resolution.standard_defect_indicator_id =
                                match.h21_indicator_id;
                            resolution.match_method = match.match_method.empty()
                                ? std::optional<std::string>("exact")
                                : std::optional<std::string>(match.match_method);
                            ++result.summary.rating_matched_count;
                        } else {
                            // 候选、复合、无规则一律停在 unresolved：候选不是权威
                            // 事实，第一阶段不持久化（§10 末段）。
                            resolution.status = "unresolved";
                            ++result.summary.rating_unresolved_count;
                        }
                        repository.upsert_rating_resolution(resolution);
                    }
                }
                tx->execSqlSync("release savepoint import_resolution_rating_match");
            } catch (const std::exception&) {
                result.summary.rating_matched_count = 0;
                result.summary.rating_unresolved_count = 0;
                tx->execSqlSync("rollback to savepoint import_resolution_rating_match");
                tx->execSqlSync("release savepoint import_resolution_rating_match");
            }
        }

        result.success = true;
        return result;
    } catch (const std::exception& error) {
        // 构件解析这一半出错必须让整个导入失败：只有 JSON 没有组的中间态会让界面
        // 显示成"一条构件行都没有"，而重跑导入又会撞上 candidate 唯一约束。
        result.success = false;
        result.error_code = "resolution_initialization_failed";
        result.error_message = error.what();
        return result;
    }
}

}  // namespace bridge_report::resolution
