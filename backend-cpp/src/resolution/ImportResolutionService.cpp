#include "bridge_report/resolution/ImportResolutionService.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/inventory/SideComponentPair.hpp"
#include "bridge_report/db/ImportResolutionRepository.hpp"
#include "bridge_report/inventory/ComponentCategoryLexicon.hpp"
#include "bridge_report/inventory/ComponentMatcher.hpp"
#include "bridge_report/inventory/ComponentRangeParser.hpp"
#include "bridge_report/rating_tree/RatingTreeResolver.hpp"
#include "bridge_report/resolution/EffectiveDefectFacts.hpp"
#include "bridge_report/resolution/ResolutionHashes.hpp"
#include "bridge_report/resolution/ResolutionTransition.hpp"

namespace bridge_report::resolution {
namespace {

std::optional<std::string> optional_row_text(
    const drogon::orm::Row& row, const char* column) {
    if (row[column].isNull()) return std::nullopt;
    return row[column].as<std::string>();
}

/// 服务层的粗筛：真正的格式校验在路由，这里只挡住空串和明显不是 uuid 的输入，
/// 免得把它送进 SQL 换来一条数据库异常，最后被报成"数据库不可用"。
bool is_uuid_like(const std::string& value) {
    return value.size() == 36 && value[8] == '-' && value[13] == '-' &&
           value[18] == '-' && value[23] == '-';
}

bool parse_json(const std::string& text, Json::Value& output) {
    Json::CharReaderBuilder builder;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.data(), text.data() + text.size(), &output, &errors);
}

/// 按 candidate_id 索引来源病害，供有效事实合并使用。
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

/**
 * @brief 按本次导入真正用到的部件类别定向装一份"局部台账"。
 *
 * 候选不持久化（§10 末段），所以歧义标签每次都得现算。现算不等于要把整份台账搬进
 * 内存——大桥五千多件，那正是按需检索改造要避免的形态。一次导入涉及的部件是几十个
 * 量级，按类别定向取数就够匹配器用了：它内部本来也要按类别过滤。
 */
inventory::InventoryRevision load_scoped_revision(
    const drogon::orm::DbClientPtr& client,
    const std::string& revision_id,
    const std::string& bridge_id,
    const std::set<std::string>& part_names) {
    std::set<std::string> categories;
    for (const auto& part_name : part_names) {
        for (const auto& category : inventory::resolve_component_categories(part_name)) {
            categories.insert(category);
        }
    }
    inventory::InventoryRevision revision;
    revision.id = revision_id;
    revision.bridge_id = bridge_id;
    // status 必须照实填。匹配器拿它区分"唯一命中可直接绑"与"只能给候选"，
    // 留空会让每一次唯一命中都降级成候选，界面上就再也分不出歧义和明确目标。
    revision.status = "已确认";
    if (categories.empty()) return revision;
    revision.entries = db::ComponentInventoryRepository(client)
                           .load_bindable_entries_by_categories(
                               revision_id,
                               std::vector<std::string>(categories.begin(), categories.end()));
    return revision;
}

std::unordered_map<std::string, WorkspaceComponentSummary> load_component_summaries(
    const drogon::orm::DbClientPtr& client,
    const std::string& revision_id,
    const std::set<std::string>& component_ids) {
    std::unordered_map<std::string, WorkspaceComponentSummary> summaries;
    if (component_ids.empty()) return summaries;
    // 定向取数：只查这次真的要展示的那几件，不整份装载。id 走数组参数而不是拼进
    // SQL 文本——拼串既要自己转义，也让查询计划无法复用。
    std::string id_array = "{";
    bool first = true;
    for (const auto& id : component_ids) {
        if (!first) id_array.push_back(',');
        id_array += id;
        first = false;
    }
    id_array.push_back('}');
    const auto rows = client->execSqlSync(
        "select e.bridge_component_id::text as bridge_component_id, e.component_number, "
        "       e.site_component_type, e.site_name, "
        "       coalesce(m.standard_component_category_id, '') as standard_component_category_id, "
        "       coalesce(m.standard_bridge_type_id, '') as standard_bridge_type_id "
        "from bridge_component_inventory_entries e "
        "left join bridge_component_standard_mappings m "
        "  on m.inventory_entry_id = e.id and m.is_active "
        "where e.inventory_revision_id = $1::uuid and e.is_active "
        "  and e.bridge_component_id = any($2::uuid[])",
        revision_id, id_array);
    for (const auto& row : rows) {
        WorkspaceComponentSummary summary;
        summary.bridge_component_id = row["bridge_component_id"].as<std::string>();
        summary.component_number = row["component_number"].as<std::string>();
        summary.site_component_type = row["site_component_type"].as<std::string>();
        summary.site_name = row["site_name"].as<std::string>();
        summary.standard_component_category_id =
            row["standard_component_category_id"].as<std::string>();
        summary.standard_bridge_type_id = row["standard_bridge_type_id"].as<std::string>();
        summaries.emplace(summary.bridge_component_id, std::move(summary));
    }
    return summaries;
}

/// 给尚未解决的组填"两侧"整体绑定候选。
///
/// 与旧绑定链路的 fill_side_pairs 同一口径：只按台账结构判定（类别在放行名单内、
/// 且该类别下可用构件恰好两件且编号仅左↔右不同），不解析病害编号里的文字。
/// 只装这几个类别的构件，find_side_component_pair 内部还会再按类别过滤。
void fill_side_pair_options(
    const drogon::orm::DbClientPtr& client,
    const std::string& revision_id,
    std::vector<WorkspaceComponentGroup>& groups) {
    const auto group_is_open = [](const WorkspaceComponentGroup& group) {
        return group.status == "unresolved";
    };

    std::unordered_map<std::string, std::string> category_by_part;
    std::vector<std::string> categories;
    for (const auto& group : groups) {
        if (!group_is_open(group)) continue;
        if (category_by_part.contains(group.source_component_name)) continue;
        for (const auto& category :
             inventory::resolve_component_categories(group.source_component_name)) {
            if (!inventory::side_pair_category_allowed(category)) continue;
            category_by_part.emplace(group.source_component_name, category);
            categories.push_back(category);
            break;
        }
    }
    if (categories.empty()) return;
    std::sort(categories.begin(), categories.end());
    categories.erase(std::unique(categories.begin(), categories.end()), categories.end());

    inventory::InventoryRevision scope;
    scope.id = revision_id;
    scope.entries = db::ComponentInventoryRepository(client)
                        .load_bindable_entries_by_categories(revision_id, categories);

    std::unordered_map<std::string, std::optional<inventory::SideComponentPair>> pairs;
    for (const auto& category : categories) {
        pairs.emplace(category, inventory::find_side_component_pair(scope, category));
    }

    for (auto& group : groups) {
        if (!group_is_open(group)) continue;
        const auto category = category_by_part.find(group.source_component_name);
        if (category == category_by_part.end()) continue;
        const auto pair = pairs.find(category->second);
        if (pair == pairs.end() || !pair->second.has_value()) continue;

        WorkspaceSidePairOption option;
        option.label = "整体绑定到 " + pair->second->left_component_number +
                       " 与 " + pair->second->right_component_number;
        option.bridge_component_ids = {
            pair->second->left_bridge_component_id,
            pair->second->right_bridge_component_id};
        group.side_pair_option = std::move(option);
    }
}

/// 组上允许哪些动作。前端只按这份清单决定按钮可用性，不自己推。
void fill_allowed_actions(WorkspaceComponentGroup& group, const bool inventory_confirmed) {
    if (!inventory_confirmed) {
        // 台账未确认时绑定类动作全部不可用，但这不是错误：病害与照片校对照常进行。
        group.blocked_reasons.push_back("component_inventory_not_confirmed");
        return;
    }
    if (group.status == "bound") {
        group.allowed_actions.push_back("rebind");
        group.allowed_actions.push_back("clear");
        group.allowed_actions.push_back("mark_missing");
        return;
    }
    // unresolved 与 missing 都能绑；missing 是人工判定，重新绑定即撤销它。
    group.allowed_actions.push_back("bind");
    if (group.status == "unresolved") {
        group.allowed_actions.push_back("mark_missing");
        if (group.split_eligible) group.allowed_actions.push_back("range_expand");
        // 批量替换只对未解析组开放：已绑定与已标记缺失的行不参与、不受影响，
        // 守住"已经核对过的结果不会被批量操作意外推翻"。
        group.allowed_actions.push_back("bulk_replace");
    } else {
        group.allowed_actions.push_back("clear_missing");
    }
}

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

// 路由层的检查是事务外的，只能挡住"进门时就没锁"；从那一刻到写入之间锁仍可能过期
// 或被管理员强制收回。与 save_review_draft / confirm_annual_facts 同一套做法。
bool edit_lock_still_active(
    const TransactionPtr& tx,
    const std::string& import_record_id,
    const std::optional<db::EditLockCredentials>& edit_lock) {
    if (!edit_lock.has_value()) return true;
    const auto rows = tx->execSqlSync(
        "select exists(select 1 from import_record_edit_locks "
        "where import_record_id=$1::uuid and user_id=$2::uuid and user_session_id=$3::uuid "
        "and lock_token_hash=$4 and expires_at>now()) as active",
        import_record_id, edit_lock->user_id, edit_lock->session_id,
        auth::sha256_hex(edit_lock->lock_token));
    return !rows.empty() && rows[0]["active"].as<bool>();
}

/// 写命令共用的前置：导入记录在待校对相、桥梁与年度、当前台账版本。
/// 目标构件在当前规范包下的生效映射；没有就返回 nullptr。
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

bool looks_like_uuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

/// Postgres uuid[] 字面量，作为参数绑定（不进 SQL 文本，故无注入面）。
/// 畸形值直接跳过：让 ::uuid[] 转换抛错会把整条命令拖成 503，而调用方随后会因为
/// 取回的行数对不上实例数而正确地报"实例不存在"。
std::string uuid_array_literal(const std::vector<std::string>& ids) {
    std::string joined = "{";
    bool first = true;
    for (const auto& id : ids) {
        if (!looks_like_uuid(id)) continue;
        if (!first) joined += ',';
        joined += id;
        first = false;
    }
    return joined + "}";
}

struct CommandPreconditions {
    std::string bridge_id;
    std::string inspection_year_id;
    /// 只有 DraftNeed::whole 装过的命令能读它；其余命令这里是空对象。
    Json::Value parsed;
    std::optional<std::string> revision_id;
};

/// 这条命令要不要整份草稿。
///
/// 本桥的草稿是 448 kB / 279 条病害，而按来源病害改评分树或实例覆盖只读其中**一条**
/// （884 字节）。区间展开的病害逐实例写 25 次，无脑整份装载等于把 448 kB 取+解析 25
/// 遍，实测占单次写入 80 ms 里的大头。真正需要整份的只有两类：按组重绑（要按 candidate
/// 索引全部病害）和手工新增病害（要查重并追加后整份回写）。
enum class DraftNeed { none, whole };

std::optional<CommandPreconditions> load_command_preconditions(
    const TransactionPtr& tx,
    const std::string& import_record_id,
    ResolutionOutcome& outcome,
    const DraftNeed draft_need) {
    const auto rows = tx->execSqlSync(
        draft_need == DraftNeed::whole
            ? "select ir.bridge_id::text as bridge_id, ir.import_status, "
              "  coalesce(ir.inspection_year_id::text,'') as inspection_year_id, "
              "  coalesce(ir.parsed_result_json::text,'{}') as parsed, "
              "  iy.component_inventory_revision_id::text as year_revision_id "
              "from import_records ir "
              "left join inspection_years iy on iy.id = ir.inspection_year_id "
              "where ir.id = $1::uuid for update of ir"
            // 不需要草稿时连取都不取：448 kB 的列白白走一趟网络再被丢掉。
            : "select ir.bridge_id::text as bridge_id, ir.import_status, "
              "  coalesce(ir.inspection_year_id::text,'') as inspection_year_id, "
              "  '{}' as parsed, "
              "  iy.component_inventory_revision_id::text as year_revision_id "
              "from import_records ir "
              "left join inspection_years iy on iy.id = ir.inspection_year_id "
              "where ir.id = $1::uuid for update of ir",
        import_record_id);
    if (rows.empty()) {
        outcome.status = ResolutionStatus::NotFound;
        return std::nullopt;
    }
    if (rows[0]["import_status"].as<std::string>() != "待校对") {
        outcome.status = ResolutionStatus::Conflict;
        outcome.error_code = "import_record_wrong_status";
        outcome.error_message = "导入记录不在待校对状态。";
        return std::nullopt;
    }
    CommandPreconditions preconditions;
    preconditions.bridge_id = rows[0]["bridge_id"].as<std::string>();
    preconditions.inspection_year_id = rows[0]["inspection_year_id"].as<std::string>();
    if (draft_need == DraftNeed::whole) {
        parse_json(rows[0]["parsed"].as<std::string>(), preconditions.parsed);
    }
    const auto revision = db::ComponentInventoryRepository(tx)
                              .resolve_confirmed_revision_ref(
                                  preconditions.bridge_id,
                                  optional_row_text(rows[0], "year_revision_id"));
    if (revision.has_value()) preconditions.revision_id = revision->id;
    return preconditions;
}

/// 只取这一条来源病害。
///
/// 库里按 jsonb 数组展开后按 candidate_id 过滤，回来的是 884 字节而不是 448 kB。
/// 找不到时返回空对象——与此前"整份扫一遍也没扫到"的行为一致，由调用方按缺失事实处理。
Json::Value load_source_defect(
    const TransactionPtr& tx,
    const std::string& import_record_id,
    const std::string& source_candidate_id) {
    const auto rows = tx->execSqlSync(
        "select d::text as defect from import_records ir, "
        "  lateral jsonb_array_elements(ir.parsed_result_json->'defects') d "
        "where ir.id = $1::uuid and d->>'candidate_id' = $2 limit 1",
        import_record_id, source_candidate_id);
    Json::Value defect(Json::objectValue);
    if (rows.empty()) return defect;
    parse_json(rows[0]["defect"].as<std::string>(), defect);
    if (!defect.isObject()) return Json::Value(Json::objectValue);
    return defect;
}

/// 客户端看到的台账版本是否仍是当前版本。
///
/// 用户打开工作区后台账被别处确认成新版本时，旧页面手里的构件候选、类别映射、可选
/// 评分树节点全是按旧版本算的。放它写进去，等于让一份基于过时语义的判断落库，而且
/// 出错时症状出现在很远的地方（确认入库阶段报"该构件不适用此病害"）。
///
/// 空的 expected 视为"调用方不校验"：并非所有内部调用点都拿得到版本。
[[nodiscard]] bool inventory_revision_matches(
    const CommandPreconditions& preconditions,
    const ResolutionCommandContext& context) {
    return context.expected_inventory_revision_id.empty()
        || preconditions.revision_id.value_or(std::string{})
               == context.expected_inventory_revision_id;
}


}  // namespace

ImportResolutionService::ImportResolutionService(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

ResolutionOutcome ImportResolutionService::load_workspace(
    const std::string& import_record_id) const {
    ResolutionOutcome outcome;
    try {
        const auto rows = db_client_->execSqlSync(
            "select ir.bridge_id::text as bridge_id, ir.import_status, ir.draft_version, "
            "  coalesce(ir.parsed_result_json::text,'{}') as parsed, "
            "  iy.component_inventory_revision_id::text as year_revision_id, "
            "  rtv.id::text as rating_tree_version_id, rtv.tree_name, "
            "  rtv.package_version as rating_tree_package_version, "
            "  rtv.technical_condition_package_version as rating_tree_h21_version, "
            "  rtv.maintenance_package_version as rating_tree_maintenance_version "
            "from import_records ir "
            "left join inspection_years iy on iy.id = ir.inspection_year_id "
            "left join project_standard_profiles profile on profile.id = iy.standard_profile_id "
            "left join rating_tree_versions rtv on rtv.id = profile.rating_tree_version_id "
            "where ir.id = $1::uuid",
            import_record_id);
        if (rows.empty()) {
            outcome.status = ResolutionStatus::NotFound;
            return outcome;
        }
        if (rows[0]["import_status"].as<std::string>() != "待校对") {
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "import_record_wrong_status";
            outcome.error_message = "导入记录不在待校对状态。";
            return outcome;
        }

        ResolutionWorkspace workspace;
        workspace.import_record_id = import_record_id;
        workspace.bridge_id = rows[0]["bridge_id"].as<std::string>();
        workspace.draft_version = rows[0]["draft_version"].as<int>();

        Json::Value parsed;
        parse_json(rows[0]["parsed"].as<std::string>(), parsed);
        const auto defects_by_candidate = index_defects(parsed);

        if (const auto version_id = optional_row_text(rows[0], "rating_tree_version_id");
            version_id.has_value()) {
            WorkspaceRatingTree tree;
            tree.version_id = *version_id;
            tree.tree_name = rows[0]["tree_name"].as<std::string>();
            tree.package_version =
                rows[0]["rating_tree_package_version"].as<std::string>();
            tree.h21_package_version =
                rows[0]["rating_tree_h21_version"].as<std::string>();
            tree.maintenance_package_version =
                rows[0]["rating_tree_maintenance_version"].as<std::string>();
            workspace.rating_tree = std::move(tree);
        }

        const auto revision_ref =
            db::ComponentInventoryRepository(db_client_).resolve_confirmed_revision_ref(
                workspace.bridge_id, optional_row_text(rows[0], "year_revision_id"));
        workspace.inventory_confirmed = revision_ref.has_value();
        if (revision_ref.has_value()) workspace.inventory_revision_id = revision_ref->id;

        const db::ImportResolutionRepository repository(db_client_);
        const auto groups = repository.list_groups(import_record_id);
        const auto members = repository.list_members(import_record_id);
        const auto instances = repository.list_instances_by_import(import_record_id);
        const auto ratings = repository.list_rating_resolutions_by_import(import_record_id);
        // 目标一次取回：按组逐次查是 N+1，一次导入几百个组时它就是首屏卡顿的全部来源。
        const auto targets = repository.list_targets_by_import(import_record_id);

        std::unordered_map<std::string, std::vector<const resolution::ComponentResolutionTarget*>>
            targets_by_group;
        std::unordered_map<std::string, const resolution::ComponentResolutionTarget*>
            target_by_id;
        for (const auto& target : targets) {
            targets_by_group[target.group_id].push_back(&target);
            target_by_id.emplace(target.id, &target);
        }

        std::unordered_map<std::string, std::vector<const resolution::ComponentGroupMember*>>
            members_by_group;
        for (const auto& member : members) {
            members_by_group[member.group_id].push_back(&member);
        }
        std::unordered_map<std::string, std::vector<const resolution::ResolvedDefectInstance*>>
            instances_by_member;
        for (const auto& instance : instances) {
            instances_by_member[instance.group_member_id].push_back(&instance);
        }
        std::unordered_map<std::string, const resolution::RatingResolution*> rating_by_instance;
        for (const auto& rating : ratings) {
            rating_by_instance.emplace(rating.resolved_defect_instance_id, &rating);
        }

        // 候选每次现算，所以先按本次导入用到的部件类别装一份局部台账。
        std::set<std::string> part_names;
        for (const auto& group : groups) part_names.insert(group.source_component_name);
        inventory::InventoryRevision scoped_revision;
        std::vector<inventory::ConfirmedComponentAlias> aliases;
        if (workspace.inventory_confirmed) {
            scoped_revision = load_scoped_revision(
                db_client_, *workspace.inventory_revision_id, workspace.bridge_id, part_names);
            const auto alias_rows = db_client_->execSqlSync(
                "select ca.bridge_component_id::text as bridge_component_id, ca.alias_text "
                "from component_aliases ca "
                "join bridge_components c on c.id = ca.bridge_component_id "
                "where c.bridge_id = $1::uuid and ca.is_manually_confirmed",
                workspace.bridge_id);
            for (const auto& row : alias_rows) {
                aliases.push_back({
                    row["bridge_component_id"].as<std::string>(),
                    row["alias_text"].as<std::string>()});
            }
        }

        std::set<std::string> component_ids_to_describe;
        std::map<std::string, WorkspacePartSummary> parts;

        for (const auto& group : groups) {
            WorkspaceComponentGroup view;
            view.group_id = group.id;
            view.source_component_name = group.source_component_name;
            view.source_component_number = group.source_component_number;
            view.normalized_component_number = group.normalized_component_number;
            view.resolution_mode = group.resolution_mode;
            view.status = group.status;
            view.match_method = group.match_method;
            view.inventory_revision_id = group.inventory_revision_id;
            view.version = group.version;

            for (const auto* target : targets_by_group[group.id]) {
                WorkspaceComponentSummary summary;
                summary.bridge_component_id = target->bridge_component_id;
                view.targets.push_back(std::move(summary));
                component_ids_to_describe.insert(target->bridge_component_id);
            }

            if (group.status == "unresolved" && workspace.inventory_confirmed) {
                const inventory::DefectComponentText text{
                    group.source_component_number.value_or(std::string{}),
                    group.source_component_name};
                const auto match =
                    inventory::match_defect_component(text, scoped_revision, aliases);
                // 歧义 = 未解析且候选多于一个。它是读模型派生标签，不是入库状态（§4.7）。
                view.ambiguous = match.candidate_component_ids.size() > 1;
                for (const auto& candidate_id : match.candidate_component_ids) {
                    WorkspaceComponentSummary summary;
                    summary.bridge_component_id = candidate_id;
                    view.candidates.push_back(std::move(summary));
                    component_ids_to_describe.insert(candidate_id);
                }

                const auto range = inventory::parse_component_range(
                    group.source_component_number.value_or(std::string{}));
                view.split_eligible =
                    range.status == inventory::ComponentRangeParseStatus::Ok;
                if (view.split_eligible) {
                    view.split_expanded_count = static_cast<int>(range.numbers.size());
                }
            }

            for (const auto* member : members_by_group[group.id]) {
                WorkspaceGroupMember member_view;
                member_view.member_id = member->id;
                member_view.source_candidate_id = member->source_candidate_id;
                member_view.source_order = member->source_order;

                const auto defect = defects_by_candidate.find(member->source_candidate_id);
                for (const auto* instance : instances_by_member[member->id]) {
                    WorkspaceDefectInstance instance_view;
                    instance_view.instance_id = instance->id;
                    instance_view.target_id = instance->target_id;
                    instance_view.instance_order = instance->instance_order;
                    instance_view.instance_status = instance->instance_status;
                    instance_view.is_photo_owner = instance->is_photo_owner;
                    instance_view.version = instance->version;
                    instance_view.component_resolution_version =
                        instance->component_resolution_version;
                    instance_view.overridden_fields =
                        overridden_fact_fields(instance->fact_overrides_json);
                    if (defect != defects_by_candidate.end()) {
                        // 界面展示的必须是有效事实：直接给来源值，用户会看不到自己
                        // 刚在这条实例上改过的东西。
                        instance_view.effective_facts = merge_effective_defect_facts(
                            *defect->second, instance->fact_overrides_json);
                    }

                    const auto rating = rating_by_instance.find(instance->id);
                    if (rating != rating_by_instance.end()) {
                        instance_view.has_rating = true;
                        instance_view.rating_status = rating->second->status;
                        instance_view.rating_tree_node_id = rating->second->rating_tree_node_id;
                        instance_view.rating_match_method = rating->second->match_method;
                        instance_view.rating_version = rating->second->version;
                        // 人工裁决之后内容变过：节点保留，但界面要提示复核（§8.5）。
                        // 判据是"裁决当时的输入哈希"与"当前输入哈希"不一致。
                        instance_view.content_changed_after_manual_resolution =
                            rating->second->resolved_match_input_hash.has_value()
                            && *rating->second->resolved_match_input_hash
                                   != rating->second->match_input_hash;
                    }

                    // 目标构件 id 直接带上，前端不必再做一次关联。
                    if (const auto target = target_by_id.find(instance->target_id);
                        target != target_by_id.end()) {
                        instance_view.bridge_component_id = target->second->bridge_component_id;
                    }

                    if (instance->instance_status == "active") {
                        ++workspace.progress.active_instance_count;
                        if (!instance_view.has_rating) {
                            ++workspace.progress.rating_missing_count;
                        } else if (instance_view.rating_status == "matched") {
                            ++workspace.progress.rating_matched_count;
                        } else {
                            ++workspace.progress.rating_unresolved_count;
                        }
                    }
                    ++workspace.progress.instance_count;
                    member_view.instances.push_back(std::move(instance_view));
                }
                view.members.push_back(std::move(member_view));
            }

            fill_allowed_actions(view, workspace.inventory_confirmed);

            auto& part = parts[group.source_component_name];
            part.source_component_name = group.source_component_name;
            ++part.total;
            if (group.status == "bound") ++part.bound;
            else if (group.status == "missing") ++part.missing;
            else ++part.unresolved;
            if (view.ambiguous) ++part.ambiguous;
            part.group_ids.push_back(group.id);

            ++workspace.progress.group_count;
            if (group.status == "bound") ++workspace.progress.bound_count;
            else if (group.status == "missing") ++workspace.progress.missing_count;
            else ++workspace.progress.unresolved_count;
            if (view.ambiguous) ++workspace.progress.ambiguous_count;

            workspace.groups.push_back(std::move(view));
        }

        // 目标与候选的展示信息一次性定向取回，避免每组一次查询。
        if (workspace.inventory_confirmed) {
            const auto summaries = load_component_summaries(
                db_client_, *workspace.inventory_revision_id, component_ids_to_describe);
            const auto fill = [&summaries](std::vector<WorkspaceComponentSummary>& list) {
                for (auto& item : list) {
                    const auto found = summaries.find(item.bridge_component_id);
                    if (found == summaries.end()) continue;
                    item.component_number = found->second.component_number;
                    item.site_component_type = found->second.site_component_type;
                    item.standard_component_category_id =
                        found->second.standard_component_category_id;
                    item.standard_bridge_type_id = found->second.standard_bridge_type_id;
                    item.site_name = found->second.site_name;
                }
            };
            for (auto& group : workspace.groups) {
                fill(group.targets);
                fill(group.candidates);
            }
            fill_side_pair_options(
                db_client_, *workspace.inventory_revision_id, workspace.groups);
        }

        for (auto& [_, part] : parts) workspace.parts.push_back(std::move(part));

        outcome.workspace = std::move(workspace);
        return outcome;
    } catch (const std::exception& error) {
        outcome.status = ResolutionStatus::Failed;
        outcome.error_code = "database_unavailable";
        outcome.error_message = error.what();
        return outcome;
    }
}

ResolutionOutcome ImportResolutionService::apply_component_resolution(
    const ComponentResolutionRequest& request) const {
    ResolutionOutcome outcome;
    if (request.action != "bind" && request.action != "mark_missing" &&
        request.action != "clear") {
        outcome.status = ResolutionStatus::Invalid;
        outcome.error_code = "invalid_resolution_request";
        outcome.error_message = "未知的构件解析动作。";
        return outcome;
    }
    if (request.action == "bind" && request.targets.empty()) {
        outcome.status = ResolutionStatus::Invalid;
        outcome.error_code = "invalid_resolution_request";
        outcome.error_message = "绑定必须至少选择一个目标构件。";
        return outcome;
    }

    TransactionPtr tx;
    const auto latch = std::make_shared<db::CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto preconditions = load_command_preconditions(
            tx, request.context.import_record_id, outcome, DraftNeed::whole);
        if (!preconditions.has_value()) {
            tx->rollback();
            return outcome;
        }
        if (!edit_lock_still_active(tx, request.context.import_record_id,
                                    request.context.edit_lock)) {
            tx->rollback();
            outcome.status = ResolutionStatus::EditLockInvalid;
            return outcome;
        }

        // 绑定必须有已确认台账；标记缺失与清除不需要，它们本来就把目标清空。
        if (request.action == "bind" && !preconditions->revision_id.has_value()) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_not_confirmed";
            outcome.error_message = "该桥尚无已确认的构件台账，无法绑定构件。";
            return outcome;
        }
        // 客户端看到的版本必须与当前解析出的一致。静默改用新版本的话，用户看到的
        // 候选来自旧版本、校验却按新版本走，被拒时无从理解发生了什么。
        if (!request.context.expected_inventory_revision_id.empty() &&
            preconditions->revision_id.value_or(std::string{}) !=
                request.context.expected_inventory_revision_id) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_revision_changed";
            outcome.error_message = "构件台账版本已变化，请刷新后重试。";
            return outcome;
        }

        // 仓库对象一律用临时量：它的构造函数按值收下 DbClientPtr 并一直持有，
        // 留一个具名变量与 tx 同域，就会让事务的 shared_ptr 活过下面的 tx.reset()，
        // 提交回调永远不来，最后以 30 秒超时收场。
        const auto group = db::ImportResolutionRepository(tx).find_group(request.group_id);
        if (!group.has_value() ||
            group->import_record_id != request.context.import_record_id) {
            tx->rollback();
            outcome.status = ResolutionStatus::NotFound;
            outcome.error_code = "component_group_not_found";
            outcome.error_message = "来源构件组不存在。";
            return outcome;
        }

        std::optional<inventory::InventoryRevision> revision;
        if (preconditions->revision_id.has_value()) {
            revision = db::ComponentInventoryRepository(tx).resolve_confirmed_revision(
                preconditions->bridge_id, preconditions->revision_id);
        }

        // 候选来自后端不等于可以信任客户端目标：这里按同一份"可用构件"口径重验。
        if (request.action == "bind") {
            if (!revision.has_value()) {
                tx->rollback();
                outcome.status = ResolutionStatus::Conflict;
                outcome.error_code = "component_inventory_not_confirmed";
                outcome.error_message = "该桥尚无已确认的构件台账，无法绑定构件。";
                return outcome;
            }
            std::set<std::string> seen;
            for (const auto& selection : request.targets) {
                if (!seen.insert(selection.bridge_component_id).second) {
                    tx->rollback();
                    outcome.status = ResolutionStatus::Invalid;
                    outcome.error_code = "invalid_resolution_request";
                    outcome.error_message = "同一组不能重复选择同一构件。";
                    return outcome;
                }
                // 单条、批量与"两侧"绑定共用这一条判定：各写一份的话，三处对
                // "这个构件能不能绑到这个部件上"的答案迟早分叉。
                if (!inventory::resolve_bindable_component(
                         *revision, group->source_component_name,
                         selection.bridge_component_id).has_value()) {
                    tx->rollback();
                    outcome.status = ResolutionStatus::Conflict;
                    outcome.error_code = "target_not_allowed";
                    outcome.error_message =
                        "目标构件不属于当前台账版本，或与部件类别不符。";
                    return outcome;
                }
            }
        }

        ResolutionTransitionInput input;
        input.import_record_id = request.context.import_record_id;
        input.actor_user_id = request.context.actor_user_id;
        input.plan_id = std::nullopt;
        if (request.action == "bind") {
            input.status = "bound";
            input.match_method = "manual";
            input.inventory_revision_id = preconditions->revision_id;
            input.targets = request.targets;
            input.resolution_mode = request.targets.size() > 1 ? "multi" : "single";
            input.operation_type = group->status == "bound" ? "rebind" : "bind";
        } else if (request.action == "mark_missing") {
            input.status = "missing";
            input.match_method = std::nullopt;
            input.inventory_revision_id = preconditions->revision_id;
            input.resolution_mode = "single";
            input.operation_type = "mark_missing";
        } else {
            input.status = "unresolved";
            input.match_method = std::nullopt;
            input.inventory_revision_id = preconditions->revision_id;
            input.resolution_mode = "single";
            input.operation_type = "clear";
        }

        const auto rating = load_rating_context(tx, preconditions->inspection_year_id);
        const auto transition = apply_component_resolution_transition(
            tx, request.group_id, request.expected_version, input,
            preconditions->parsed, revision, rating);
        if (!transition.success) {
            tx->rollback();
            outcome.status = ResolutionStatus::VersionConflict;
            outcome.error_code = transition.error_code;
            outcome.error_message = transition.error_message;
            return outcome;
        }

        tx.reset();
        if (!latch->wait()) {
            outcome.status = ResolutionStatus::Failed;
            outcome.error_code = "database_unavailable";
            outcome.error_message = "事务提交未确认。";
            return outcome;
        }
        return build_command_result(request.context.import_record_id, {request.group_id});
    } catch (const std::exception& error) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        outcome.status = ResolutionStatus::Failed;
        outcome.error_code = "database_unavailable";
        outcome.error_message = error.what();
        return outcome;
    }
}

ResolutionOutcome ImportResolutionService::write_rating_resolutions(
    const ResolutionCommandContext& context,
    const std::vector<RatingInstanceVersion>& instances,
    const std::string& rating_tree_node_id,
    const std::string& expected_rating_tree_version_id,
    const std::optional<std::string>& source_candidate_id) const {
    ResolutionOutcome outcome;
    if (instances.empty()) {
        outcome.status = ResolutionStatus::Invalid;
        outcome.error_code = "invalid_resolution_request";
        outcome.error_message = "没有要写入的病害解析实例。";
        return outcome;
    }
    TransactionPtr tx;
    const auto latch = std::make_shared<db::CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto preconditions = load_command_preconditions(
            tx, context.import_record_id, outcome, DraftNeed::none);
        if (!preconditions.has_value()) { tx->rollback(); return outcome; }
        if (!inventory_revision_matches(*preconditions, context)) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_revision_changed";
            outcome.error_message = "构件台账版本已变化，请刷新后重试。";
            return outcome;
        }
        if (!edit_lock_still_active(tx, context.import_record_id, context.edit_lock)) {
            tx->rollback();
            outcome.status = ResolutionStatus::EditLockInvalid;
            return outcome;
        }

        // 仓库对象一律用临时量：它的构造函数按值收下 DbClientPtr 并一直持有，
        // 留一个具名变量与 tx 同域，就会让事务的 shared_ptr 活过下面的 tx.reset()，
        // 提交回调永远不来，最后以 30 秒超时收场。
        const auto rating = load_rating_context(tx, preconditions->inspection_year_id);
        if (!rating.has_value()) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "rating_tree_not_bound";
            outcome.error_message = "当前检测年度尚未绑定评定树版本。";
            return outcome;
        }
        if (!expected_rating_tree_version_id.empty() &&
            expected_rating_tree_version_id != rating->rating_tree_version_id) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "rating_tree_version_changed";
            outcome.error_message = "评定树版本已变化，请刷新后重新选择。";
            return outcome;
        }

        std::vector<std::string> instance_ids;
        instance_ids.reserve(instances.size());
        std::set<std::string> submitted;
        for (const auto& item : instances) {
            if (!submitted.insert(item.instance_id).second) {
                tx->rollback();
                outcome.status = ResolutionStatus::Invalid;
                outcome.error_code = "invalid_resolution_request";
                outcome.error_message = "同一条病害解析实例重复提交。";
                return outcome;
            }
            instance_ids.push_back(item.instance_id);
        }

        // 按来源病害整体写时，"整条病害是哪些实例"由服务端说了算。
        //
        // 只核对提交的 id 存不存在是不够的：那只能挡住多提交，挡不住少提交。区间展开
        // 之后拿着旧页面提交，手里只有展开前那一条实例，数量自洽、每条都存在，于是写
        // 完返回成功——而库里另外 24 条还停在未解析，界面却显示整行已经选好。
        if (source_candidate_id.has_value()) {
            const auto active_rows = tx->execSqlSync(
                "select i.id::text as id from import_resolved_defect_instances i "
                "join import_component_group_members m on m.id = i.group_member_id "
                "where m.import_record_id = $1::uuid and m.source_candidate_id = $2 "
                "  and i.instance_status = 'active'",
                context.import_record_id, *source_candidate_id);
            std::set<std::string> active;
            for (const auto& row : active_rows) active.insert(row["id"].as<std::string>());
            if (active.empty()) {
                tx->rollback();
                outcome.status = ResolutionStatus::NotFound;
                outcome.error_code = "defect_instance_not_found";
                outcome.error_message = "这条病害没有活动的解析实例。";
                return outcome;
            }
            // 多出来的一律不是"过期"，而是不该发生：别条病害的实例、已忽略的实例，
            // 客户端没有任何理由把它们放进这条命令。
            for (const auto& id : submitted) {
                if (!active.contains(id)) {
                    tx->rollback();
                    outcome.status = ResolutionStatus::Invalid;
                    outcome.error_code = "invalid_resolution_request";
                    outcome.error_message =
                        "提交的解析实例不属于这条病害的活动实例。";
                    return outcome;
                }
            }
            // 少了则多半是页面陈旧（这期间发生过区间展开、多目标绑定或实例忽略），
            // 让它刷新重来，而不是写一半。
            if (submitted.size() != active.size()) {
                tx->rollback();
                outcome.status = ResolutionStatus::Conflict;
                outcome.error_code = "resolution_instance_set_stale";
                outcome.error_message =
                    "这条病害的活动实例已变化，请刷新后重新选择。";
                return outcome;
            }
        }

        // 一次取回本次要写的全部实例（连同各自的构件、组、覆盖与现有解析版本），
        // 而不是每条实例发一轮往返。
        const auto instance_rows = tx->execSqlSync(
            "select i.id::text as id, i.instance_status, "
            "  t.bridge_component_id::text as bridge_component_id, "
            "  m.source_candidate_id, g.id::text as group_id, "
            "  g.version as group_version, "
            "  i.fact_overrides_json::text as fact_overrides_json, "
            "  coalesce(r.version, 0) as rating_version "
            "from import_resolved_defect_instances i "
            "join import_component_resolution_targets t on t.id = i.target_id "
            "join import_component_group_members m on m.id = i.group_member_id "
            "join import_component_resolution_groups g on g.id = m.group_id "
            "left join import_rating_resolutions r on r.resolved_defect_instance_id = i.id "
            "where i.id = any($1::uuid[]) and m.import_record_id = $2::uuid",
            uuid_array_literal(instance_ids), context.import_record_id);
        if (instance_rows.size() != instances.size()) {
            tx->rollback();
            outcome.status = ResolutionStatus::NotFound;
            outcome.error_code = "defect_instance_not_found";
            outcome.error_message = "病害解析实例不存在。";
            return outcome;
        }

        struct InstanceFacts {
            std::string bridge_component_id;
            std::string source_candidate_id;
            std::string group_id;
            int group_version{0};
            int rating_version{0};
            Json::Value overrides;
        };
        std::map<std::string, InstanceFacts> facts_by_instance;
        std::vector<std::string> component_ids;
        std::set<std::string> source_candidate_ids;
        std::set<std::string> group_ids;
        for (const auto& row : instance_rows) {
            InstanceFacts facts;
            facts.bridge_component_id = row["bridge_component_id"].as<std::string>();
            facts.source_candidate_id = row["source_candidate_id"].as<std::string>();
            facts.group_id = row["group_id"].as<std::string>();
            facts.group_version = row["group_version"].as<int>();
            facts.rating_version = row["rating_version"].as<int>();
            parse_json(row["fact_overrides_json"].as<std::string>(), facts.overrides);
            if (!facts.overrides.isObject()) facts.overrides = Json::Value(Json::objectValue);
            component_ids.push_back(facts.bridge_component_id);
            source_candidate_ids.insert(facts.source_candidate_id);
            group_ids.insert(facts.group_id);
            facts_by_instance.emplace(row["id"].as<std::string>(), std::move(facts));
        }

        // 乐观并发按每条实例自己的解析行版本判定，与逐实例接口同一口径。
        for (const auto& item : instances) {
            const auto found = facts_by_instance.find(item.instance_id);
            if (found == facts_by_instance.end()) {
                tx->rollback();
                outcome.status = ResolutionStatus::NotFound;
                outcome.error_code = "defect_instance_not_found";
                outcome.error_message = "病害解析实例不存在。";
                return outcome;
            }
            if (found->second.rating_version != item.expected_version) {
                tx->rollback();
                outcome.status = ResolutionStatus::VersionConflict;
                outcome.error_code = "resolution_version_conflict";
                outcome.error_message = "评分树解析版本已过期，请刷新后重试。";
                return outcome;
            }
        }

        // 涉及的构件一次装完。区间展开的病害是 25 件，装 25 条而不是 25 遍 5174 条。
        std::optional<inventory::InventoryRevision> revision;
        if (preconditions->revision_id.has_value()) {
            revision = db::ComponentInventoryRepository(tx)
                           .resolve_confirmed_revision_for_components(
                               preconditions->bridge_id, preconditions->revision_id,
                               component_ids);
        }

        // 来源病害按 candidate 取一次即可：同一条来源病害的实例共用同一份来源事实，
        // 差别只在各自的 fact_overrides_json。
        std::map<std::string, Json::Value> source_defects;
        for (const auto& candidate : source_candidate_ids) {
            source_defects.emplace(
                candidate, load_source_defect(tx, context.import_record_id, candidate));
        }

        const bool clearing = rating_tree_node_id.empty();
        for (const auto& item : instances) {
            const auto& facts = facts_by_instance.at(item.instance_id);
            const inventory::InventoryMapping* mapping = nullptr;
            if (revision.has_value()) {
                mapping = find_active_mapping(
                    *revision, facts.bridge_component_id,
                    rating->technical_standard_package_id);
            }
            if (mapping == nullptr) {
                tx->rollback();
                outcome.status = ResolutionStatus::Conflict;
                outcome.error_code = "target_not_allowed";
                outcome.error_message = "目标构件在当前规范包下没有已确认的类别映射。";
                return outcome;
            }

            RatingResolution resolution;
            resolution.resolved_defect_instance_id = item.instance_id;
            resolution.rating_tree_version_id = rating->rating_tree_version_id;
            resolution.component_resolution_version = facts.group_version;
            // 人工选择也要留哈希：适用性一变它同样必须失效，只是文字变化不动它（§8.5）。
            const auto effective = merge_effective_defect_facts(
                source_defects.at(facts.source_candidate_id), facts.overrides);
            const auto hashes = compute_rating_match_hashes(build_rating_match_hash_input(
                effective, facts.source_candidate_id, facts.bridge_component_id,
                rating->technical_standard_package_id, mapping->standard_bridge_type_id,
                mapping->standard_component_category_id, rating->rating_tree_version_id));
            resolution.applicability_hash = hashes.applicability_hash;
            resolution.match_input_hash = hashes.match_input_hash;

            if (clearing) {
                resolution.status = "unresolved";
                resolution.match_method = std::nullopt;
                resolution.resolved_match_input_hash = std::nullopt;
            } else {
                resolution.status = "matched";
                resolution.rating_tree_node_id = rating_tree_node_id;
                resolution.match_method = "manual";
                // 裁决那一刻的输入哈希（§8.5）。之后文字再变，节点保留，但界面据此提示复核。
                resolution.resolved_match_input_hash = hashes.match_input_hash;
                resolution.resolved_by_user_id = context.actor_user_id;
                const auto node = rating->tree.nodes.find(rating_tree_node_id);
                if (node == rating->tree.nodes.end()) {
                    tx->rollback();
                    outcome.status = ResolutionStatus::Invalid;
                    outcome.error_code = "invalid_resolution_request";
                    outcome.error_message = "所选节点不属于当前评定树版本。";
                    return outcome;
                }
                // 节点存在还不够，还得适用于目标构件的桥型与类别——否则会存进一个自动
                // 匹配永远挑不到的节点，直到评定阶段才以"该构件不适用此病害"暴露出来。
                //
                // 按来源病害整体写时这一条逐实例都要过：只要有一件构件不适用就整批回滚，
                // 而不是写进去一半——校对页那一行显示的是整条病害的结论。
                if (!rating_tree::node_applies_to(
                        node->second, mapping->standard_bridge_type_id,
                        mapping->standard_component_category_id)) {
                    tx->rollback();
                    outcome.status = ResolutionStatus::Conflict;
                    outcome.error_code = "target_not_allowed";
                    outcome.error_message = "所选评定树节点不适用于该构件的桥型与类别。";
                    return outcome;
                }
                resolution.standard_defect_indicator_id = node->second.h21_indicator_id;
                Json::Value evidence(Json::objectValue);
                evidence["outcome"] = "manual";
                evidence["reason_code"] = "manual_selection";
                evidence["reason_message"] = "人工选择评定树病害节点。";
                resolution.match_evidence_json = std::move(evidence);
            }
            db::ImportResolutionRepository(tx).upsert_rating_resolution(resolution);

            ResolutionEvent event;
            event.import_record_id = context.import_record_id;
            event.group_id = facts.group_id;
            event.resolved_defect_instance_id = item.instance_id;
            event.operation_type = clearing ? "rating_clear" : "rating_manual_select";
            event.actor_user_id = context.actor_user_id;
            Json::Value after(Json::objectValue);
            after["status"] = resolution.status;
            event.after_json = std::move(after);
            db::ImportResolutionRepository(tx).append_event(event);
        }

        tx.reset();
        if (!latch->wait()) {
            outcome.status = ResolutionStatus::Failed;
            outcome.error_code = "database_unavailable";
            outcome.error_message = "事务提交未确认。";
            return outcome;
        }
        return build_command_result(
            context.import_record_id,
            std::vector<std::string>(group_ids.begin(), group_ids.end()));
    } catch (const std::exception& error) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        outcome.status = ResolutionStatus::Failed;
        outcome.error_code = "database_unavailable";
        outcome.error_message = error.what();
        return outcome;
    }
}

ResolutionOutcome ImportResolutionService::apply_rating_resolution(
    const RatingResolutionRequest& request) const {
    // 逐实例接口只写点名的那一条，不做集合校验：它的语义就是"改这一条实例"。
    return write_rating_resolutions(
        request.context,
        {RatingInstanceVersion{request.instance_id, request.expected_version}},
        request.rating_tree_node_id,
        request.expected_rating_tree_version_id,
        std::nullopt);
}

ResolutionOutcome ImportResolutionService::apply_source_rating_resolution(
    const SourceRatingResolutionRequest& request) const {
    ResolutionOutcome outcome;
    if (request.instances.empty()) {
        outcome.status = ResolutionStatus::Invalid;
        outcome.error_code = "invalid_resolution_request";
        outcome.error_message = "这条病害没有可写入的活动实例。";
        return outcome;
    }
    if (request.source_candidate_id.empty()) {
        outcome.status = ResolutionStatus::Invalid;
        outcome.error_code = "invalid_resolution_request";
        outcome.error_message = "缺少来源病害标识。";
        return outcome;
    }
    return write_rating_resolutions(
        request.context, request.instances, request.rating_tree_node_id,
        request.expected_rating_tree_version_id, request.source_candidate_id);
}

ResolutionOutcome ImportResolutionService::apply_fact_overrides(
    const FactOverrideRequest& request) const {
    ResolutionOutcome outcome;
    TransactionPtr tx;
    const auto latch = std::make_shared<db::CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto preconditions = load_command_preconditions(
            tx, request.context.import_record_id, outcome, DraftNeed::none);
        if (!preconditions.has_value()) { tx->rollback(); return outcome; }
        if (!inventory_revision_matches(*preconditions, request.context)) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_revision_changed";
            outcome.error_message = "构件台账版本已变化，请刷新后重试。";
            return outcome;
        }
        if (!edit_lock_still_active(tx, request.context.import_record_id,
                                    request.context.edit_lock)) {
            tx->rollback();
            outcome.status = ResolutionStatus::EditLockInvalid;
            return outcome;
        }

        const auto rows = tx->execSqlSync(
            "select i.fact_overrides_json::text as overrides, i.version, "
            "  i.instance_status, t.bridge_component_id::text as bridge_component_id, "
            "  m.source_candidate_id, g.id::text as group_id, g.version as group_version "
            "from import_resolved_defect_instances i "
            "join import_component_resolution_targets t on t.id = i.target_id "
            "join import_component_group_members m on m.id = i.group_member_id "
            "join import_component_resolution_groups g on g.id = m.group_id "
            "where i.id = $1::uuid and m.import_record_id = $2::uuid for update of i",
            request.instance_id, request.context.import_record_id);
        if (rows.empty()) {
            tx->rollback();
            outcome.status = ResolutionStatus::NotFound;
            outcome.error_code = "defect_instance_not_found";
            outcome.error_message = "病害解析实例不存在。";
            return outcome;
        }
        if (rows[0]["version"].as<int>() != request.expected_version) {
            tx->rollback();
            outcome.status = ResolutionStatus::VersionConflict;
            outcome.error_code = "resolution_version_conflict";
            outcome.error_message = "实例版本已过期，请刷新后重试。";
            return outcome;
        }

        Json::Value overrides;
        parse_json(rows[0]["overrides"].as<std::string>(), overrides);
        if (!overrides.isObject()) overrides = Json::Value(Json::objectValue);
        // 清除是删键，不是写 null：写 null 会让必填事实带着 null 进预检。
        for (const auto& field : request.cleared_fields) overrides.removeMember(field);
        if (request.overrides.isObject()) {
            for (const auto& key : request.overrides.getMemberNames()) {
                overrides[key] = request.overrides[key];
            }
        }
        std::string reason;
        if (!fact_overrides_are_valid(overrides, reason)) {
            tx->rollback();
            outcome.status = ResolutionStatus::Invalid;
            outcome.error_code = "invalid_fact_override";
            outcome.error_message = reason;
            return outcome;
        }

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        tx->execSqlSync(
            "update import_resolved_defect_instances set fact_overrides_json = $2::jsonb, "
            "  version = version + 1, updated_at = now() where id = $1::uuid",
            request.instance_id, Json::writeString(writer, overrides));

        const auto group_id = rows[0]["group_id"].as<std::string>();
        // 涉及匹配输入的实例级变化后按新的有效值重算：自动结果重新解析，
        // 人工结果保留并标记内容变化（§11.2）。
        const auto rating = load_rating_context(tx, preconditions->inspection_year_id);
        if (rating.has_value() && preconditions->revision_id.has_value() &&
            rows[0]["instance_status"].as<std::string>() == "active") {
            // 与评分树那条路径同理：只用它查一件构件的 (桥型, 规范类别)，
            // 装整份 5174 条纯属浪费。
            const auto revision =
                db::ComponentInventoryRepository(tx)
                    .resolve_confirmed_revision_for_components(
                        preconditions->bridge_id, preconditions->revision_id,
                        {rows[0]["bridge_component_id"].as<std::string>()});
            if (revision.has_value()) {
                const auto candidate = rows[0]["source_candidate_id"].as<std::string>();
                const auto source_defect = load_source_defect(
                    tx, request.context.import_record_id, candidate);
                ResolvedDefectInstance instance;
                instance.id = request.instance_id;
                instance.fact_overrides_json = overrides;
                int rewritten = 0;
                int preserved = 0;
                refresh_rating_resolution(
                    tx, instance, source_defect,
                    rows[0]["bridge_component_id"].as<std::string>(), *revision, *rating,
                    rows[0]["group_version"].as<int>(), rewritten, preserved);
            }
        }

        // 仓库对象一律用临时量：它的构造函数按值收下 DbClientPtr 并一直持有，
        // 留一个具名变量与 tx 同域，就会让事务的 shared_ptr 活过下面的 tx.reset()，
        // 提交回调永远不来，最后以 30 秒超时收场。
        ResolutionEvent event;
        event.import_record_id = request.context.import_record_id;
        event.group_id = group_id;
        event.resolved_defect_instance_id = request.instance_id;
        event.operation_type = "fact_override";
        event.actor_user_id = request.context.actor_user_id;
        event.after_json = overrides;
        db::ImportResolutionRepository(tx).append_event(event);

        tx.reset();
        if (!latch->wait()) {
            outcome.status = ResolutionStatus::Failed;
            outcome.error_code = "database_unavailable";
            outcome.error_message = "事务提交未确认。";
            return outcome;
        }
        return build_command_result(request.context.import_record_id, {group_id});
    } catch (const std::exception& error) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        outcome.status = ResolutionStatus::Failed;
        outcome.error_code = "database_unavailable";
        outcome.error_message = error.what();
        return outcome;
    }
}

ResolutionOutcome ImportResolutionService::apply_instance_status(
    const InstanceStatusRequest& request) const {
    ResolutionOutcome outcome;
    if (request.instance_status != "active" && request.instance_status != "ignored") {
        outcome.status = ResolutionStatus::Invalid;
        outcome.error_code = "invalid_resolution_request";
        outcome.error_message = "实例状态只能是 active 或 ignored。";
        return outcome;
    }

    TransactionPtr tx;
    const auto latch = std::make_shared<db::CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto preconditions = load_command_preconditions(
            tx, request.context.import_record_id, outcome, DraftNeed::none);
        if (!preconditions.has_value()) { tx->rollback(); return outcome; }
        if (!inventory_revision_matches(*preconditions, request.context)) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_revision_changed";
            outcome.error_message = "构件台账版本已变化，请刷新后重试。";
            return outcome;
        }
        if (!edit_lock_still_active(tx, request.context.import_record_id,
                                    request.context.edit_lock)) {
            tx->rollback();
            outcome.status = ResolutionStatus::EditLockInvalid;
            return outcome;
        }

        const auto rows = tx->execSqlSync(
            "select g.id::text as group_id from import_resolved_defect_instances i "
            "join import_component_group_members m on m.id = i.group_member_id "
            "join import_component_resolution_groups g on g.id = m.group_id "
            "where i.id = $1::uuid and m.import_record_id = $2::uuid",
            request.instance_id, request.context.import_record_id);
        if (rows.empty()) {
            tx->rollback();
            outcome.status = ResolutionStatus::NotFound;
            outcome.error_code = "defect_instance_not_found";
            outcome.error_message = "病害解析实例不存在。";
            return outcome;
        }

        // 仓库对象一律用临时量：它的构造函数按值收下 DbClientPtr 并一直持有，
        // 留一个具名变量与 tx 同域，就会让事务的 shared_ptr 活过下面的 tx.reset()，
        // 提交回调永远不来，最后以 30 秒超时收场。
        const auto version = db::ImportResolutionRepository(tx).set_instance_status(
            request.instance_id, request.expected_version, request.instance_status);
        if (!version.has_value()) {
            tx->rollback();
            outcome.status = ResolutionStatus::VersionConflict;
            outcome.error_code = "resolution_version_conflict";
            outcome.error_message = "实例版本已过期，请刷新后重试。";
            return outcome;
        }

        const auto group_id = rows[0]["group_id"].as<std::string>();
        ResolutionEvent event;
        event.import_record_id = request.context.import_record_id;
        event.group_id = group_id;
        event.resolved_defect_instance_id = request.instance_id;
        event.operation_type = "instance_status";
        event.actor_user_id = request.context.actor_user_id;
        Json::Value after(Json::objectValue);
        after["instance_status"] = request.instance_status;
        event.after_json = std::move(after);
        db::ImportResolutionRepository(tx).append_event(event);

        tx.reset();
        if (!latch->wait()) {
            outcome.status = ResolutionStatus::Failed;
            outcome.error_code = "database_unavailable";
            outcome.error_message = "事务提交未确认。";
            return outcome;
        }
        return build_command_result(request.context.import_record_id, {group_id});
    } catch (const std::exception& error) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        outcome.status = ResolutionStatus::Failed;
        outcome.error_code = "database_unavailable";
        outcome.error_message = error.what();
        return outcome;
    }
}

ResolutionOutcome ImportResolutionService::add_manual_defect(
    const ManualDefectRequest& request) const {
    ResolutionOutcome outcome;
    if (!is_uuid_like(request.bridge_component_id) ||
        request.rating_tree_node_id.empty()) {
        outcome.status = ResolutionStatus::Invalid;
        outcome.error_code = "invalid_resolution_request";
        outcome.error_message = "手工新增必须选定实际构件与评定树节点。";
        return outcome;
    }
    for (const auto* required : {"defect_type", "defect_location", "defect_description"}) {
        if (!request.defect_facts[required].isString() ||
            request.defect_facts[required].asString().empty()) {
            outcome.status = ResolutionStatus::Invalid;
            outcome.error_code = "invalid_resolution_request";
            outcome.error_message = "病害类型、位置和描述都必须填写。";
            return outcome;
        }
    }

    TransactionPtr tx;
    const auto latch = std::make_shared<db::CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        auto preconditions = load_command_preconditions(
            tx, request.context.import_record_id, outcome, DraftNeed::whole);
        if (!preconditions.has_value()) { tx->rollback(); return outcome; }
        if (!inventory_revision_matches(*preconditions, request.context)) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_revision_changed";
            outcome.error_message = "构件台账版本已变化，请刷新后重试。";
            return outcome;
        }
        if (!edit_lock_still_active(tx, request.context.import_record_id,
                                    request.context.edit_lock)) {
            tx->rollback();
            outcome.status = ResolutionStatus::EditLockInvalid;
            return outcome;
        }

        // 来源草稿版本：陈旧的整份草稿保存不能把这条新增当成"用户删掉了"，反过来
        // 也一样——先到的那个赢，后到的必须看见明确的版本冲突（§8.0）。
        const auto current_draft_version =
            db::ImportResolutionRepository(tx).read_draft_version(
                request.context.import_record_id);
        if (!current_draft_version.has_value()) {
            tx->rollback();
            outcome.status = ResolutionStatus::NotFound;
            return outcome;
        }
        if (*current_draft_version != request.expected_draft_version) {
            tx->rollback();
            outcome.status = ResolutionStatus::VersionConflict;
            outcome.error_code = "review_draft_version_conflict";
            outcome.error_message = "来源草稿已被其他操作更新，请刷新后重试。";
            return outcome;
        }

        if (!preconditions->revision_id.has_value()) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_not_confirmed";
            outcome.error_message = "该桥尚无已确认的构件台账，无法手工新增病害。";
            return outcome;
        }
        if (!request.context.expected_inventory_revision_id.empty() &&
            *preconditions->revision_id !=
                request.context.expected_inventory_revision_id) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_revision_changed";
            outcome.error_message = "构件台账版本已变化，请刷新后重试。";
            return outcome;
        }

        const auto revision =
            db::ComponentInventoryRepository(tx).resolve_confirmed_revision(
                preconditions->bridge_id, preconditions->revision_id);
        if (!revision.has_value()) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_not_confirmed";
            outcome.error_message = "构件台账不可用。";
            return outcome;
        }

        // 组的来源名称与编号取自用户选中的台账条目本身（§4.6），所以先把条目找出来。
        const inventory::InventoryEntry* entry = nullptr;
        const inventory::InventoryMapping* mapping = nullptr;
        for (const auto& candidate : revision->entries) {
            if (!candidate.is_active ||
                candidate.bridge_component_id != request.bridge_component_id) {
                continue;
            }
            entry = &candidate;
            mapping = inventory::active_inventory_mapping(candidate);
            break;
        }
        if (entry == nullptr || mapping == nullptr) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "target_not_allowed";
            outcome.error_message = "所选构件不在当前台账版本中，或没有生效的规范映射。";
            return outcome;
        }

        const auto rating = load_rating_context(tx, preconditions->inspection_year_id);
        if (!rating.has_value()) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "rating_tree_not_bound";
            outcome.error_message = "当前检测年度尚未绑定评定树版本。";
            return outcome;
        }
        const auto node = rating->tree.nodes.find(request.rating_tree_node_id);
        if (node == rating->tree.nodes.end() ||
            !rating_tree::node_applies_to(
                node->second, mapping->standard_bridge_type_id,
                mapping->standard_component_category_id)) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "target_not_allowed";
            outcome.error_message = "所选评定树节点不适用于该构件的桥型与类别。";
            return outcome;
        }

        const auto source_component_name = entry->site_component_type;
        const auto source_component_number = entry->component_number;
        const auto normalized =
            inventory::normalize_component_number(source_component_number);

        // §4.6 的既有组处理：不能因为"新增一条病害"暗中改变既有病害的解析结果。
        const auto groups = db::ImportResolutionRepository(tx).list_groups(
            request.context.import_record_id);
        const ComponentResolutionGroup* existing = nullptr;
        for (const auto& group : groups) {
            if (group.source_component_name == source_component_name &&
                group.normalized_component_number == normalized) {
                existing = &group;
                break;
            }
        }
        if (existing != nullptr) {
            if (existing->status == "missing") {
                tx->rollback();
                outcome.status = ResolutionStatus::Conflict;
                outcome.error_code = "manual_defect_group_conflict";
                outcome.error_message =
                    "该构件组已标记为台账缺失，请先在绑定工作区处理。";
                return outcome;
            }
            if (existing->status == "bound") {
                const auto targets =
                    db::ImportResolutionRepository(tx).list_targets(existing->id);
                const bool same_single_target =
                    targets.size() == 1 &&
                    targets[0].bridge_component_id == request.bridge_component_id;
                if (!same_single_target) {
                    tx->rollback();
                    outcome.status = ResolutionStatus::Conflict;
                    outcome.error_code = "manual_defect_group_conflict";
                    outcome.error_message =
                        "该构件组已绑定到其他构件，请改用绑定工作区。";
                    return outcome;
                }
            } else {
                // unresolved 且已有成员：单条新增不该顺手把一批旧病害也绑上。
                tx->rollback();
                outcome.status = ResolutionStatus::Conflict;
                outcome.error_code = "manual_defect_group_requires_resolution";
                outcome.error_message =
                    "该构件组尚未解析，请先在绑定工作区解析后再新增。";
                return outcome;
            }
        }

        // 追加来源病害。candidate_id 必须在本次导入内唯一。
        std::set<std::string> used_candidate_ids;
        int max_source_order = -1;
        for (const auto& defect : preconditions->parsed["defects"]) {
            if (defect["candidate_id"].isString()) {
                used_candidate_ids.insert(defect["candidate_id"].asString());
            }
        }
        for (const auto& member : db::ImportResolutionRepository(tx).list_members(
                 request.context.import_record_id)) {
            max_source_order = (std::max)(max_source_order, member.source_order);
        }
        std::string candidate_id;
        for (int suffix = 1;; ++suffix) {
            candidate_id = "manual_defect_" + std::to_string(suffix);
            if (!used_candidate_ids.contains(candidate_id)) break;
        }

        Json::Value defect(Json::objectValue);
        defect["candidate_id"] = candidate_id;
        defect["source_structure_part"] = Json::Value();
        defect["component_name"] = source_component_name;
        defect["component_number"] = source_component_number;
        for (const auto* field : {
                 "defect_type", "defect_location", "defect_description",
                 "quantity_text", "measurement_text", "remark"}) {
            defect[field] = request.defect_facts.isMember(field)
                ? request.defect_facts[field] : Json::Value();
        }
        defect["defect_scale"] = request.defect_facts.isMember("defect_scale")
            ? request.defect_facts["defect_scale"] : Json::Value();
        defect["measurements"] = request.defect_facts["measurements"].isArray()
            ? request.defect_facts["measurements"] : Json::Value(Json::arrayValue);
        defect["photo_references"] = Json::Value(Json::arrayValue);
        defect["group_review_status"] = "待确认";
        defect["severity"] = Json::Value();
        defect["source_ref"] = Json::Value(Json::objectValue);
        defect["source_ref"]["source_type"] = "manual";
        defect["confidence"] = 1.0;
        defect["review_status"] = "已修改";
        defect["warnings"] = Json::Value(Json::arrayValue);
        if (!preconditions->parsed["defects"].isArray()) {
            preconditions->parsed["defects"] = Json::Value(Json::arrayValue);
        }
        preconditions->parsed["defects"].append(defect);

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        tx->execSqlSync(
            "update import_records set parsed_result_json = $2::jsonb, "
            "  draft_version = draft_version + 1, updated_at = now() "
            "where id = $1::uuid and draft_version = $3",
            request.context.import_record_id,
            Json::writeString(writer, preconditions->parsed),
            request.expected_draft_version);

        std::string group_id;
        int group_version = 1;
        if (existing != nullptr) {
            group_id = existing->id;
            group_version = existing->version;
        } else {
            ComponentResolutionGroup group;
            group.import_record_id = request.context.import_record_id;
            group.source_component_name = source_component_name;
            group.source_component_number = source_component_number;
            group.normalized_component_number = normalized;
            group.resolution_mode = "single";
            group.status = "bound";
            group.match_method = "manual";
            group.inventory_revision_id = preconditions->revision_id;
            group.resolved_by_user_id = request.context.actor_user_id;
            const auto stored_group =
                db::ImportResolutionRepository(tx).insert_group(group);
            group_id = stored_group.id;
            group_version = stored_group.version;

            ComponentResolutionTarget target;
            target.group_id = group_id;
            target.bridge_component_id = request.bridge_component_id;
            target.target_order = 1;
            target.target_role = "primary";
            db::ImportResolutionRepository(tx).insert_target(target);
        }

        ComponentGroupMember member;
        member.import_record_id = request.context.import_record_id;
        member.group_id = group_id;
        member.source_candidate_id = candidate_id;
        member.source_order = max_source_order + 1;
        const auto stored_member =
            db::ImportResolutionRepository(tx).insert_member(member);

        const auto target_id =
            db::ImportResolutionRepository(tx).list_targets(group_id)[0].id;
        ResolvedDefectInstance instance;
        instance.group_member_id = stored_member.id;
        instance.target_id = target_id;
        instance.instance_order = 1;
        instance.instance_status = "active";
        instance.is_photo_owner = true;
        instance.component_resolution_version = group_version;
        const auto stored_instance =
            db::ImportResolutionRepository(tx).insert_instance(instance);

        // 人工评分树解析。哈希照常算：适用性一变它同样必须失效（§8.5）。
        const auto hashes = compute_rating_match_hashes(build_rating_match_hash_input(
            defect, candidate_id, request.bridge_component_id,
            rating->technical_standard_package_id, mapping->standard_bridge_type_id,
            mapping->standard_component_category_id, rating->rating_tree_version_id));
        RatingResolution resolution;
        resolution.resolved_defect_instance_id = stored_instance.id;
        resolution.rating_tree_version_id = rating->rating_tree_version_id;
        resolution.rating_tree_node_id = request.rating_tree_node_id;
        resolution.standard_defect_indicator_id = node->second.h21_indicator_id;
        resolution.status = "matched";
        resolution.match_method = "manual";
        resolution.component_resolution_version = group_version;
        resolution.applicability_hash = hashes.applicability_hash;
        resolution.match_input_hash = hashes.match_input_hash;
        resolution.resolved_by_user_id = request.context.actor_user_id;
        Json::Value evidence(Json::objectValue);
        evidence["outcome"] = "manual";
        evidence["reason_code"] = "manual_defect";
        evidence["reason_message"] = "手工新增病害时人工选定评定树节点。";
        resolution.match_evidence_json = std::move(evidence);
        db::ImportResolutionRepository(tx).upsert_rating_resolution(resolution);

        ResolutionEvent event;
        event.import_record_id = request.context.import_record_id;
        event.group_id = group_id;
        event.resolved_defect_instance_id = stored_instance.id;
        event.operation_type = "manual_defect_added";
        event.actor_user_id = request.context.actor_user_id;
        Json::Value after(Json::objectValue);
        after["source_candidate_id"] = candidate_id;
        after["bridge_component_id"] = request.bridge_component_id;
        after["rating_tree_node_id"] = request.rating_tree_node_id;
        after["reused_existing_group"] = existing != nullptr;
        event.after_json = std::move(after);
        db::ImportResolutionRepository(tx).append_event(event);

        tx.reset();
        if (!latch->wait()) {
            outcome.status = ResolutionStatus::Failed;
            outcome.error_code = "database_unavailable";
            outcome.error_message = "事务提交未确认。";
            return outcome;
        }

        auto command_outcome =
            build_command_result(request.context.import_record_id, {group_id});
        if (command_outcome.status != ResolutionStatus::Ok ||
            !command_outcome.command_result.has_value()) {
            return command_outcome;
        }
        ManualDefectResult manual;
        manual.source_defect = defect;
        manual.draft_version = request.expected_draft_version + 1;
        manual.command_result = std::move(*command_outcome.command_result);
        ResolutionOutcome result;
        result.manual_defect = std::move(manual);
        return result;
    } catch (const std::exception& error) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        outcome.status = ResolutionStatus::Failed;
        outcome.error_code = "database_unavailable";
        outcome.error_message = error.what();
        return outcome;
    }
}

ResolutionOutcome ImportResolutionService::build_command_result(
    const std::string& import_record_id,
    const std::vector<std::string>& group_ids) const {
    // 写操作只回受影响对象和最新统计（§13.2）。读模型与工作区共用同一份构建代码：
    // 各写一份的话，命令返回的组和刷新后看到的组迟早对不上。
    auto outcome = load_workspace(import_record_id);
    if (outcome.status != ResolutionStatus::Ok || !outcome.workspace.has_value()) {
        return outcome;
    }
    ResolutionCommandResult result;
    result.progress = outcome.workspace->progress;
    for (const auto& group : outcome.workspace->groups) {
        if (std::find(group_ids.begin(), group_ids.end(), group.group_id) !=
            group_ids.end()) {
            result.affected_groups.push_back(group);
        }
    }
    ResolutionOutcome command_outcome;
    command_outcome.command_result = std::move(result);
    return command_outcome;
}

}  // namespace bridge_report::resolution
