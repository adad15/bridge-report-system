// 预览计划的生成与执行（设计 §8.6、§13.3、§14）。
//
// 与单条命令分在两个 .cpp 里，但共用同一份状态转换（ResolutionTransition）和同一份
// 组视图构建（ImportResolutionService::build_command_result）：计划执行走另一条写路径
// 的话，"批量绑定保住实例覆盖了吗"这类问题就要各答一遍。

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/db/ImportResolutionRepository.hpp"
#include "bridge_report/inventory/ComponentCategoryLexicon.hpp"
#include "bridge_report/inventory/ComponentMatcher.hpp"
#include "bridge_report/inventory/ComponentRangeParser.hpp"
#include "bridge_report/inventory/ComponentReplacePattern.hpp"
#include "bridge_report/resolution/ImportResolutionService.hpp"
#include "bridge_report/resolution/ResolutionTransition.hpp"

namespace bridge_report::resolution {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

bool parse_json_value(const std::string& text, Json::Value& output) {
    Json::CharReaderBuilder builder;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.data(), text.data() + text.size(), &output, &errors);
}

/// 计划执行需要的一切上下文，一次装齐。
struct PlanContext {
    std::string bridge_id;
    std::string inspection_year_id;
    Json::Value parsed;
    std::optional<std::string> revision_id;
    std::optional<inventory::InventoryRevision> revision;
    std::optional<std::string> rating_tree_version_id;
};

std::optional<PlanContext> load_plan_context(
    const TransactionPtr& tx,
    const std::string& import_record_id,
    ResolutionOutcome& outcome) {
    const auto rows = tx->execSqlSync(
        "select ir.bridge_id::text as bridge_id, ir.import_status, "
        "  coalesce(ir.inspection_year_id::text,'') as inspection_year_id, "
        "  coalesce(ir.parsed_result_json::text,'{}') as parsed, "
        "  iy.component_inventory_revision_id::text as year_revision_id, "
        "  profile.rating_tree_version_id::text as rating_tree_version_id "
        "from import_records ir "
        "left join inspection_years iy on iy.id = ir.inspection_year_id "
        "left join project_standard_profiles profile on profile.id = iy.standard_profile_id "
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
    PlanContext context;
    context.bridge_id = rows[0]["bridge_id"].as<std::string>();
    context.inspection_year_id = rows[0]["inspection_year_id"].as<std::string>();
    parse_json_value(rows[0]["parsed"].as<std::string>(), context.parsed);
    if (!rows[0]["rating_tree_version_id"].isNull()) {
        context.rating_tree_version_id =
            rows[0]["rating_tree_version_id"].as<std::string>();
    }
    std::optional<std::string> year_revision;
    if (!rows[0]["year_revision_id"].isNull()) {
        year_revision = rows[0]["year_revision_id"].as<std::string>();
    }
    const auto reference = db::ComponentInventoryRepository(tx)
                               .resolve_confirmed_revision_ref(context.bridge_id, year_revision);
    if (reference.has_value()) {
        context.revision_id = reference->id;
        context.revision = db::ComponentInventoryRepository(tx).resolve_confirmed_revision(
            context.bridge_id, context.revision_id);
    }
    return context;
}

bool edit_lock_active(
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

std::string lock_token_hash_of(
    const std::optional<db::EditLockCredentials>& edit_lock) {
    // 夹具可以不带锁；那种情况下用一个固定串占位，计划仍然只能被同一条路径应用。
    return edit_lock.has_value() ? auth::sha256_hex(edit_lock->lock_token)
                                 : std::string("no-edit-lock");
}

/// 在指定台账版本里按"部件类别 + 归一化编号"找可绑构件。
std::vector<std::string> find_components_by_number(
    const inventory::InventoryRevision& revision,
    const std::string& part_name,
    const std::string& number) {
    std::vector<std::string> hits;
    const auto normalized = inventory::normalize_component_number(number);
    if (normalized.empty()) return hits;
    const auto categories = inventory::resolve_component_categories(part_name);
    for (const auto* entry : inventory::usable_inventory_entries(revision)) {
        const auto* mapping = inventory::active_inventory_mapping(*entry);
        if (mapping == nullptr) continue;
        if (!categories.empty() &&
            std::find(categories.begin(), categories.end(),
                      mapping->standard_component_category_id) == categories.end()) {
            continue;
        }
        if (inventory::normalize_component_number(entry->component_number) == normalized) {
            hits.push_back(entry->bridge_component_id);
        }
    }
    return hits;
}

Json::Value plan_row_json(const ResolutionPlanRow& row) {
    Json::Value value(Json::objectValue);
    value["group_id"] = row.group_id;
    value["source_component_name"] = row.source_component_name;
    value["source_component_number"] = row.source_component_number;
    value["member_count"] = row.member_count;
    value["resolved_numbers"] = Json::Value(Json::arrayValue);
    for (const auto& number : row.resolved_numbers) {
        value["resolved_numbers"].append(number);
    }
    value["target_component_ids"] = Json::Value(Json::arrayValue);
    for (const auto& id : row.target_component_ids) {
        value["target_component_ids"].append(id);
    }
    value["outcome"] = row.outcome;
    value["reason_code"] = row.reason_code;
    value["reason_message"] = row.reason_message;
    return value;
}

ResolutionPlanRow row_from_json(const Json::Value& value) {
    ResolutionPlanRow row;
    row.group_id = value["group_id"].asString();
    row.source_component_name = value["source_component_name"].asString();
    row.source_component_number = value["source_component_number"].asString();
    row.member_count = value["member_count"].asInt();
    for (const auto& number : value["resolved_numbers"]) {
        row.resolved_numbers.push_back(number.asString());
    }
    for (const auto& id : value["target_component_ids"]) {
        row.target_component_ids.push_back(id.asString());
    }
    row.outcome = value["outcome"].asString();
    row.reason_code = value["reason_code"].asString();
    row.reason_message = value["reason_message"].asString();
    return row;
}

/// 把计划落库并回填 token 与过期时间。
ResolutionPlanPreview persist_plan(
    const TransactionPtr& tx,
    const std::string& import_record_id,
    const std::string& actor_user_id,
    const std::string& operation_type,
    const std::string& lock_token_hash,
    const Json::Value& request_json,
    const PlanContext& context,
    ResolutionPlanPreview preview,
    const std::vector<ResolutionPlanRow>& rows,
    const std::map<std::string, int>& group_versions) {
    Json::Value plan_json(Json::objectValue);
    plan_json["rows"] = Json::Value(Json::arrayValue);
    for (const auto& row : rows) plan_json["rows"].append(plan_row_json(row));
    plan_json["will_apply_count"] = preview.will_apply_count;
    plan_json["skipped_count"] = preview.skipped_count;
    plan_json["blocked_count"] = preview.blocked_count;
    plan_json["instances_before"] = preview.instances_before;
    plan_json["instances_after"] = preview.instances_after;
    plan_json["rating_recomputed_count"] = preview.rating_recomputed_count;

    // 前提冻结在计划里：执行时逐条重新验证，任一不符就整批拒绝。
    Json::Value preconditions(Json::objectValue);
    preconditions["inventory_revision_id"] = context.revision_id.has_value()
        ? Json::Value(*context.revision_id) : Json::Value(Json::nullValue);
    preconditions["rating_tree_version_id"] = context.rating_tree_version_id.has_value()
        ? Json::Value(*context.rating_tree_version_id) : Json::Value(Json::nullValue);
    preconditions["group_versions"] = Json::Value(Json::objectValue);
    for (const auto& [group_id, version] : group_versions) {
        preconditions["group_versions"][group_id] = version;
    }

    const auto stored = tx->execSqlSync(
        "insert into import_resolution_operation_plans "
        "(import_record_id, actor_user_id, operation_type, lock_token_hash, "
        " request_json, plan_json, preconditions_json, expires_at) "
        "values ($1::uuid, $2::uuid, $3, $4, $5::jsonb, $6::jsonb, $7::jsonb, "
        "        now() + interval '15 minutes') "
        "returning id::text as id, expires_at::text as expires_at",
        import_record_id, actor_user_id, operation_type, lock_token_hash,
        compact_json(request_json), compact_json(plan_json), compact_json(preconditions));

    preview.plan_token = stored[0]["id"].as<std::string>();
    preview.expires_at = stored[0]["expires_at"].as<std::string>();
    preview.operation_type = operation_type;
    preview.rows = rows;
    preview.inventory_revision_id = context.revision_id;
    preview.rating_tree_version_id = context.rating_tree_version_id;
    return preview;
}

}  // namespace

ResolutionOutcome ImportResolutionService::build_bulk_replace_plan(
    const ResolutionCommandContext& context, const BulkReplaceIntent& intent) const {
    ResolutionOutcome outcome;
    std::string compile_error;
    const auto pattern = inventory::ComponentReplacePattern::compile(
        intent.find, intent.replace, compile_error);
    if (!pattern.has_value()) {
        outcome.status = ResolutionStatus::Invalid;
        outcome.error_code = "invalid_resolution_request";
        outcome.error_message = compile_error;
        return outcome;
    }

    TransactionPtr tx;
    const auto latch = std::make_shared<db::CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        auto plan_context = load_plan_context(tx, context.import_record_id, outcome);
        if (!plan_context.has_value()) { tx->rollback(); return outcome; }
        if (!edit_lock_active(tx, context.import_record_id, context.edit_lock)) {
            tx->rollback();
            outcome.status = ResolutionStatus::EditLockInvalid;
            return outcome;
        }
        if (!plan_context->revision.has_value()) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_not_confirmed";
            outcome.error_message = "该桥尚无已确认的构件台账。";
            return outcome;
        }

        std::vector<ResolutionPlanRow> rows;
        std::map<std::string, int> group_versions;
        ResolutionPlanPreview preview;
        for (const auto& group :
             db::ImportResolutionRepository(tx).list_groups(context.import_record_id)) {
            // 只有未解析组参与：已绑定与已标记缺失的组不参与、不受影响。
            if (group.status != "unresolved") continue;
            if (!intent.source_component_name.empty() &&
                group.source_component_name != intent.source_component_name) {
                continue;
            }
            ResolutionPlanRow row;
            row.group_id = group.id;
            row.source_component_name = group.source_component_name;
            row.source_component_number = group.source_component_number.value_or("");
            for (const auto& member :
                 db::ImportResolutionRepository(tx).list_members(context.import_record_id)) {
                if (member.group_id == group.id) ++row.member_count;
            }

            // 模式匹配作用于行上显示的报告原文，不预先归一化：所见即所匹配，
            // 行内若有异常空白会落到"不符合查找模式"并在预览里显形，而不是被静默吞掉。
            const auto replaced = pattern->apply(row.source_component_number);
            if (!replaced.has_value()) {
                row.outcome = "skipped";
                row.reason_code = "pattern_not_matched";
                row.reason_message = "不符合查找模式。";
                ++preview.skipped_count;
                rows.push_back(std::move(row));
                continue;
            }
            row.resolved_numbers.push_back(*replaced);
            const auto hits = find_components_by_number(
                *plan_context->revision, group.source_component_name, *replaced);
            if (hits.empty()) {
                row.outcome = "skipped";
                row.reason_code = "component_not_found";
                row.reason_message = "台账中无此编号。";
                ++preview.skipped_count;
            } else if (hits.size() > 1) {
                row.outcome = "skipped";
                row.reason_code = "component_ambiguous";
                row.reason_message = "转换后的编号命中多个构件，需人工选择。";
                ++preview.skipped_count;
            } else {
                row.target_component_ids = hits;
                row.outcome = "will_bind";
                ++preview.will_apply_count;
                preview.instances_before += row.member_count;
                preview.instances_after += row.member_count;
                preview.rating_recomputed_count += row.member_count;
                group_versions.emplace(group.id, group.version);
            }
            rows.push_back(std::move(row));
        }

        Json::Value request_json(Json::objectValue);
        request_json["source_component_name"] = intent.source_component_name;
        request_json["find"] = intent.find;
        request_json["replace"] = intent.replace;

        auto stored = persist_plan(
            tx, context.import_record_id, context.actor_user_id.value_or(std::string{}),
            "bulk_replace", lock_token_hash_of(context.edit_lock), request_json,
            *plan_context, std::move(preview), rows, group_versions);

        tx.reset();
        if (!latch->wait()) {
            outcome.status = ResolutionStatus::Failed;
            outcome.error_code = "database_unavailable";
            outcome.error_message = "事务提交未确认。";
            return outcome;
        }
        ResolutionOutcome result;
        result.plan = std::move(stored);
        return result;
    } catch (const std::exception& error) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        outcome.status = ResolutionStatus::Failed;
        outcome.error_code = "database_unavailable";
        outcome.error_message = error.what();
        return outcome;
    }
}

ResolutionOutcome ImportResolutionService::build_range_expand_plan(
    const ResolutionCommandContext& context, const RangeExpandIntent& intent) const {
    ResolutionOutcome outcome;
    if (intent.group_ids.empty()) {
        outcome.status = ResolutionStatus::Invalid;
        outcome.error_code = "invalid_resolution_request";
        outcome.error_message = "区间展开必须至少选择一个构件组。";
        return outcome;
    }

    TransactionPtr tx;
    const auto latch = std::make_shared<db::CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        auto plan_context = load_plan_context(tx, context.import_record_id, outcome);
        if (!plan_context.has_value()) { tx->rollback(); return outcome; }
        if (!edit_lock_active(tx, context.import_record_id, context.edit_lock)) {
            tx->rollback();
            outcome.status = ResolutionStatus::EditLockInvalid;
            return outcome;
        }
        if (!plan_context->revision.has_value()) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_not_confirmed";
            outcome.error_message = "该桥尚无已确认的构件台账。";
            return outcome;
        }

        const std::set<std::string> wanted(intent.group_ids.begin(), intent.group_ids.end());
        std::vector<ResolutionPlanRow> rows;
        std::map<std::string, int> group_versions;
        ResolutionPlanPreview preview;
        for (const auto& group :
             db::ImportResolutionRepository(tx).list_groups(context.import_record_id)) {
            if (!wanted.contains(group.id)) continue;
            ResolutionPlanRow row;
            row.group_id = group.id;
            row.source_component_name = group.source_component_name;
            row.source_component_number = group.source_component_number.value_or("");
            for (const auto& member :
                 db::ImportResolutionRepository(tx).list_members(context.import_record_id)) {
                if (member.group_id == group.id) ++row.member_count;
            }

            if (group.status != "unresolved") {
                row.outcome = "blocked";
                row.reason_code = "group_already_resolved";
                row.reason_message = "已绑定或已标记缺失的组不参与区间展开。";
                ++preview.blocked_count;
                rows.push_back(std::move(row));
                continue;
            }
            const auto range = inventory::parse_component_range(row.source_component_number);
            if (range.status != inventory::ComponentRangeParseStatus::Ok) {
                row.outcome = "blocked";
                row.reason_code = "not_a_range";
                row.reason_message = range.message.empty()
                    ? "该编号不是可展开的构件范围。" : range.message;
                ++preview.blocked_count;
                rows.push_back(std::move(row));
                continue;
            }

            bool all_found = true;
            for (const auto& number : range.numbers) {
                row.resolved_numbers.push_back(number);
                const auto hits = find_components_by_number(
                    *plan_context->revision, group.source_component_name, number);
                if (hits.size() != 1) {
                    all_found = false;
                    row.reason_code = hits.empty() ? "component_not_found"
                                                   : "component_ambiguous";
                    row.reason_message = hits.empty()
                        ? "展开后的编号 " + number + " 在台账中不存在。"
                        : "展开后的编号 " + number + " 命中多个构件。";
                    break;
                }
                row.target_component_ids.push_back(hits.front());
            }
            if (!all_found) {
                // 区间要么整条展开、要么不展开：只绑上其中几跨，剩下几跨会静默消失。
                row.outcome = "blocked";
                row.target_component_ids.clear();
                ++preview.blocked_count;
                rows.push_back(std::move(row));
                continue;
            }
            row.outcome = "will_bind";
            ++preview.will_apply_count;
            preview.instances_before += row.member_count;
            // 累的是本行新增的实例数，不是已经累计过的总数。写成 += instances_after
            // 的话，第二个组会把第一个组的量再加一遍：两个各 3 实例的组显示成 9。
            const int row_instances =
                row.member_count * static_cast<int>(row.target_component_ids.size());
            preview.instances_after += row_instances;
            preview.rating_recomputed_count += row_instances;
            group_versions.emplace(group.id, group.version);
            rows.push_back(std::move(row));
        }

        Json::Value request_json(Json::objectValue);
        request_json["group_ids"] = Json::Value(Json::arrayValue);
        for (const auto& id : intent.group_ids) request_json["group_ids"].append(id);

        auto stored = persist_plan(
            tx, context.import_record_id, context.actor_user_id.value_or(std::string{}),
            "range_expand", lock_token_hash_of(context.edit_lock), request_json,
            *plan_context, std::move(preview), rows, group_versions);

        tx.reset();
        if (!latch->wait()) {
            outcome.status = ResolutionStatus::Failed;
            outcome.error_code = "database_unavailable";
            outcome.error_message = "事务提交未确认。";
            return outcome;
        }
        ResolutionOutcome result;
        result.plan = std::move(stored);
        return result;
    } catch (const std::exception& error) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        outcome.status = ResolutionStatus::Failed;
        outcome.error_code = "database_unavailable";
        outcome.error_message = error.what();
        return outcome;
    }
}

ResolutionOutcome ImportResolutionService::build_inventory_repoint_plan(
    const ResolutionCommandContext& context,
    const InventoryRepointIntent& intent) const {
    ResolutionOutcome outcome;
    TransactionPtr tx;
    const auto latch = std::make_shared<db::CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        auto plan_context = load_plan_context(tx, context.import_record_id, outcome);
        if (!plan_context.has_value()) { tx->rollback(); return outcome; }
        if (!edit_lock_active(tx, context.import_record_id, context.edit_lock)) {
            tx->rollback();
            outcome.status = ResolutionStatus::EditLockInvalid;
            return outcome;
        }
        if (!plan_context->revision.has_value()) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_not_confirmed";
            outcome.error_message = "该桥尚无已确认的构件台账。";
            return outcome;
        }

        const std::set<std::string> wanted(intent.group_ids.begin(), intent.group_ids.end());
        std::vector<ResolutionPlanRow> rows;
        std::map<std::string, int> group_versions;
        ResolutionPlanPreview preview;
        for (const auto& group :
             db::ImportResolutionRepository(tx).list_groups(context.import_record_id)) {
            if (!wanted.empty() && !wanted.contains(group.id)) continue;
            // 已经钉在当前版本的组不需要重指。
            if (group.inventory_revision_id.has_value() &&
                *group.inventory_revision_id == *plan_context->revision_id) {
                continue;
            }
            ResolutionPlanRow row;
            row.group_id = group.id;
            row.source_component_name = group.source_component_name;
            row.source_component_number = group.source_component_number.value_or("");
            for (const auto& member :
                 db::ImportResolutionRepository(tx).list_members(context.import_record_id)) {
                if (member.group_id == group.id) ++row.member_count;
            }

            if (group.status != "bound") {
                // 未解析与已标记缺失的组只改版本号，目标本来就是空的。
                row.outcome = "will_repoint";
                row.reason_code = "revision_only";
                row.reason_message = "仅更新所依据的台账版本。";
                ++preview.will_apply_count;
                group_versions.emplace(group.id, group.version);
                rows.push_back(std::move(row));
                continue;
            }

            bool all_present = true;
            for (const auto& target :
                 db::ImportResolutionRepository(tx).list_targets(group.id)) {
                const auto bindable = inventory::resolve_bindable_component(
                    *plan_context->revision, group.source_component_name,
                    target.bridge_component_id);
                if (!bindable.has_value()) {
                    all_present = false;
                    break;
                }
                row.target_component_ids.push_back(target.bridge_component_id);
                row.resolved_numbers.push_back(bindable->entry->component_number);
            }
            if (all_present && !row.target_component_ids.empty()) {
                row.outcome = "will_repoint";
                row.reason_code = "target_still_valid";
                row.reason_message = "目标在新版本中仍然可用，实例与覆盖原样保留。";
                preview.instances_before += row.member_count;
                const int row_instances =
                    row.member_count * static_cast<int>(row.target_component_ids.size());
                preview.instances_after += row_instances;
                preview.rating_recomputed_count += row_instances;
            } else {
                // 构件在新版本里停用或类别变了：组回落未解析，等人工重绑。
                row.outcome = "will_clear";
                row.target_component_ids.clear();
                row.reason_code = "target_unavailable";
                row.reason_message = "目标构件在新版本中已停用或类别变化，需人工重绑。";
                preview.instances_before += row.member_count;
            }
            ++preview.will_apply_count;
            group_versions.emplace(group.id, group.version);
            rows.push_back(std::move(row));
        }

        Json::Value request_json(Json::objectValue);
        request_json["group_ids"] = Json::Value(Json::arrayValue);
        for (const auto& id : intent.group_ids) request_json["group_ids"].append(id);

        auto stored = persist_plan(
            tx, context.import_record_id, context.actor_user_id.value_or(std::string{}),
            "inventory_repoint", lock_token_hash_of(context.edit_lock), request_json,
            *plan_context, std::move(preview), rows, group_versions);

        tx.reset();
        if (!latch->wait()) {
            outcome.status = ResolutionStatus::Failed;
            outcome.error_code = "database_unavailable";
            outcome.error_message = "事务提交未确认。";
            return outcome;
        }
        ResolutionOutcome result;
        result.plan = std::move(stored);
        return result;
    } catch (const std::exception& error) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        outcome.status = ResolutionStatus::Failed;
        outcome.error_code = "database_unavailable";
        outcome.error_message = error.what();
        return outcome;
    }
}

ResolutionOutcome ImportResolutionService::apply_resolution_plan(
    const ResolutionCommandContext& context, const std::string& plan_token) const {
    ResolutionOutcome outcome;
    TransactionPtr tx;
    const auto latch = std::make_shared<db::CommitLatch>();
    std::vector<std::string> affected_group_ids;
    try {
        tx = db_client_->newTransaction(latch->callback());

        // §14 第 1 步：行锁。没有它，两个并发应用请求会都看到 ready 后各执行一遍。
        const auto plan_rows = tx->execSqlSync(
            "select operation_type, actor_user_id::text as actor_user_id, "
            "  lock_token_hash, plan_json::text as plan_json, "
            "  preconditions_json::text as preconditions_json, status, "
            "  apply_result_json::text as apply_result_json, "
            "  expires_at <= now() as expired "
            "from import_resolution_operation_plans "
            "where id = $1::uuid and import_record_id = $2::uuid for update",
            plan_token, context.import_record_id);
        if (plan_rows.empty()) {
            tx->rollback();
            outcome.status = ResolutionStatus::NotFound;
            outcome.error_code = "resolution_plan_not_found";
            outcome.error_message = "预览计划不存在。";
            return outcome;
        }
        const auto& plan_row = plan_rows[0];
        if (plan_row["actor_user_id"].as<std::string>() !=
            context.actor_user_id.value_or(std::string{})) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "resolution_plan_invalidated";
            outcome.error_message = "预览计划属于其他用户。";
            return outcome;
        }

        const auto status = plan_row["status"].as<std::string>();
        // §14 第 2 步：已应用先重放。它是结果重放而不是再次执行，因此**不要求**原编辑
        // 锁仍然存活——这条分支存在的意义正是"服务端已成功、客户端响应丢失"后的重试。
        if (status == "applied") {
            Json::Value replayed;
            parse_json_value(plan_row["apply_result_json"].as<std::string>(), replayed);
            tx->rollback();
            ResolutionOutcome replay;
            replay.apply_result = std::move(replayed);
            return replay;
        }
        if (status == "expired" || status == "invalidated") {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = status == "expired" ? "resolution_plan_expired"
                                                     : "resolution_plan_invalidated";
            outcome.error_message = status == "expired"
                ? "预览计划已过期，请重新生成。" : "预览计划前提已变化，请重新预览。";
            return outcome;
        }
        if (plan_row["expired"].as<bool>()) {
            tx->execSqlSync(
                "update import_resolution_operation_plans set status='expired' "
                "where id=$1::uuid", plan_token);
            tx.reset();
            (void)latch->wait();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "resolution_plan_expired";
            outcome.error_message = "预览计划已过期，请重新生成。";
            return outcome;
        }

        // §14 第 5 步：锁仍在，且是**同一把**锁。心跳续租不换 token，所以正常编辑
        // 期间这条恒成立；锁掉线后被重新拿到时 token 已换，计划就该作废。
        if (!edit_lock_active(tx, context.import_record_id, context.edit_lock)) {
            tx->rollback();
            outcome.status = ResolutionStatus::EditLockInvalid;
            return outcome;
        }
        if (plan_row["lock_token_hash"].as<std::string>() !=
            lock_token_hash_of(context.edit_lock)) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "resolution_plan_invalidated";
            outcome.error_message = "编辑锁已更换，预览计划作废，请重新预览。";
            return outcome;
        }

        auto plan_context = load_plan_context(tx, context.import_record_id, outcome);
        if (!plan_context.has_value()) { tx->rollback(); return outcome; }

        Json::Value preconditions;
        parse_json_value(plan_row["preconditions_json"].as<std::string>(), preconditions);
        Json::Value plan_json;
        parse_json_value(plan_row["plan_json"].as<std::string>(), plan_json);

        // §14 第 6 步：台账与评分树版本仍与计划一致。
        const auto frozen_revision = preconditions["inventory_revision_id"].isString()
            ? preconditions["inventory_revision_id"].asString() : std::string{};
        if (frozen_revision != plan_context->revision_id.value_or(std::string{})) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "component_inventory_revision_changed";
            outcome.error_message = "构件台账版本已变化，预览计划作废。";
            return outcome;
        }
        const auto frozen_tree = preconditions["rating_tree_version_id"].isString()
            ? preconditions["rating_tree_version_id"].asString() : std::string{};
        if (frozen_tree != plan_context->rating_tree_version_id.value_or(std::string{})) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "rating_tree_version_changed";
            outcome.error_message = "评定树版本已变化，预览计划作废。";
            return outcome;
        }

        // §14 第 7 步：每个受影响构件组的版本仍与计划一致。
        for (const auto& group_id : preconditions["group_versions"].getMemberNames()) {
            const auto group = db::ImportResolutionRepository(tx).find_group(group_id);
            if (!group.has_value() ||
                group->version != preconditions["group_versions"][group_id].asInt()) {
                tx->rollback();
                outcome.status = ResolutionStatus::Conflict;
                outcome.error_code = "resolution_plan_invalidated";
                outcome.error_message = "计划涉及的构件组已被改动，请重新预览。";
                return outcome;
            }
        }

        const auto operation_type = plan_row["operation_type"].as<std::string>();
        const auto rating = load_rating_context(tx, plan_context->inspection_year_id);

        int applied_groups = 0;
        for (const auto& row_json : plan_json["rows"]) {
            const auto row = row_from_json(row_json);
            if (row.outcome != "will_bind" && row.outcome != "will_repoint" &&
                row.outcome != "will_clear") {
                continue;
            }
            const auto group = db::ImportResolutionRepository(tx).find_group(row.group_id);
            if (!group.has_value()) {
                tx->rollback();
                outcome.status = ResolutionStatus::Conflict;
                outcome.error_code = "resolution_plan_invalidated";
                outcome.error_message = "计划涉及的构件组已不存在，请重新预览。";
                return outcome;
            }

            ResolutionTransitionInput input;
            input.import_record_id = context.import_record_id;
            input.actor_user_id = context.actor_user_id;
            input.plan_id = plan_token;
            input.operation_type = operation_type;
            input.inventory_revision_id = plan_context->revision_id;
            if (row.outcome == "will_clear") {
                input.status = "unresolved";
                input.match_method = std::nullopt;
                input.resolution_mode = "single";
            } else if (row.outcome == "will_repoint" && row.target_component_ids.empty()) {
                // 仅换所依据的台账版本。这一路来自"导入时该桥还没有已确认台账"：组停在
                // unresolved（或已标记缺失），台账确认后统一重指（§9.4）。
                //
                // 不能顺手写成 bound——它一个目标都没有，而 bound 组按约束必须至少有一个，
                // 整批计划会在提交阶段炸掉。而且语义上也不对：换版本不代表有人挑好了构件。
                input.status = group->status;
                input.match_method = group->match_method;
                input.resolution_mode = group->resolution_mode;
            } else {
                input.status = "bound";
                input.match_method =
                    operation_type == "range_expand" ? "range" : group->match_method;
                if (!input.match_method.has_value()) input.match_method = "manual";
                input.resolution_mode = row.target_component_ids.size() > 1
                    ? (operation_type == "range_expand" ? "range" : "multi") : "single";
                for (const auto& component_id : row.target_component_ids) {
                    ResolutionTargetSelection selection;
                    selection.bridge_component_id = component_id;
                    selection.target_role = row.target_component_ids.size() > 1
                        ? "range_member" : "primary";
                    // §14 第 8 步：目标仍属于当前桥梁、当前台账版本和允许的类别。
                    if (!plan_context->revision.has_value() ||
                        !inventory::resolve_bindable_component(
                             *plan_context->revision, group->source_component_name,
                             component_id).has_value()) {
                        tx->rollback();
                        outcome.status = ResolutionStatus::Conflict;
                        outcome.error_code = "target_not_allowed";
                        outcome.error_message = "计划中的目标构件已不可用，请重新预览。";
                        return outcome;
                    }
                    input.targets.push_back(std::move(selection));
                }
            }

            const auto transition = apply_component_resolution_transition(
                tx, row.group_id, group->version, input, plan_context->parsed,
                plan_context->revision, rating);
            if (!transition.success) {
                // 任一失败则整批不写入——不尝试"尽可能执行"。
                tx->rollback();
                outcome.status = ResolutionStatus::VersionConflict;
                outcome.error_code = transition.error_code;
                outcome.error_message = transition.error_message;
                return outcome;
            }
            affected_group_ids.push_back(row.group_id);
            ++applied_groups;
        }

        Json::Value apply_result(Json::objectValue);
        apply_result["operation_type"] = operation_type;
        apply_result["applied_group_count"] = applied_groups;
        apply_result["affected_group_ids"] = Json::Value(Json::arrayValue);
        for (const auto& group_id : affected_group_ids) {
            apply_result["affected_group_ids"].append(group_id);
        }

        // 条件写：并发的第二个请求走不到这里（前面有行锁），但状态条件是第二道闸门。
        const auto marked = tx->execSqlSync(
            "update import_resolution_operation_plans set status='applied', "
            "  applied_at=now(), apply_result_json=$2::jsonb "
            "where id=$1::uuid and status='ready' returning id",
            plan_token, compact_json(apply_result));
        if (marked.empty()) {
            tx->rollback();
            outcome.status = ResolutionStatus::Conflict;
            outcome.error_code = "resolution_plan_invalidated";
            outcome.error_message = "预览计划已被其他请求处理。";
            return outcome;
        }

        tx.reset();
        if (!latch->wait()) {
            outcome.status = ResolutionStatus::Failed;
            outcome.error_code = "database_unavailable";
            outcome.error_message = "事务提交未确认。";
            return outcome;
        }

        auto command_outcome =
            build_command_result(context.import_record_id, affected_group_ids);
        if (command_outcome.status != ResolutionStatus::Ok) return command_outcome;
        command_outcome.apply_result = std::move(apply_result);
        return command_outcome;
    } catch (const std::exception& error) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        outcome.status = ResolutionStatus::Failed;
        outcome.error_code = "database_unavailable";
        outcome.error_message = error.what();
        return outcome;
    }
}

}  // namespace bridge_report::resolution
