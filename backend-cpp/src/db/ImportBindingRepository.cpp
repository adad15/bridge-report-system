#include "bridge_report/db/ImportBindingRepository.hpp"

#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/review/DraftValidation.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/inventory/ComponentCategoryLexicon.hpp"
#include "bridge_report/inventory/ComponentMatcher.hpp"
#include "bridge_report/inventory/ComponentRangeParser.hpp"
#include "bridge_report/inventory/SideComponentPair.hpp"
#include "bridge_report/review/ContractCompatibility.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

// 在写事务内复查编辑锁。路由层已经拦过一道，但那是**事务外**的检查：从那一刻到
// 真正写入之间，锁可能过期或被管理员强制收回。与 save_review_draft /
// confirm_annual_facts 同一套做法。传 nullopt 表示调用方不校验锁（测试夹具用）。
bool edit_lock_still_active(
    const TransactionPtr& tx, const std::string& import_id,
    const std::optional<EditLockCredentials>& edit_lock) {
    if (!edit_lock.has_value()) return true;
    const auto rows = tx->execSqlSync(
        "select exists(select 1 from import_record_edit_locks "
        "where import_record_id=$1::uuid and user_id=$2::uuid and user_session_id=$3::uuid "
        "and lock_token_hash=$4 and expires_at>now()) as active",
        import_id, edit_lock->user_id, edit_lock->session_id,
        auth::sha256_hex(edit_lock->lock_token));
    return !rows.empty() && rows[0]["active"].as<bool>();
}

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

bool parse_json(const std::string& text, Json::Value& out) {
    Json::CharReaderBuilder builder;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errors);
}

std::optional<std::string> optional_row_text(
    const drogon::orm::Row& row, const std::string& column) {
    return row[column].isNull()
        ? std::nullopt
        : std::optional<std::string>(row[column].as<std::string>());
}

// 在年度行锁内重读该年度锁定的台账版本。
//
// 上面那句联查只 for update of ir：年度字段是在**没有年度行锁**的情况下读的。两条
// 导入记录可以关联同一个年度，而 bind_rating_tree 会无条件改写年度的台账版本，于是
// 记录 A 可能按 R1 写完全部病害构件关联，同时记录 B 把年度切到了 R2——草稿里的版本
// 与年度上下文就此不一致，下次保存校对草稿会整体报"台账版本已变化"。
//
// 锁顺序固定 import_records -> inspection_years，与保存草稿、年度确认和 Word 导入一致；
// 反过来加锁会和年度删除、整桥删除那条路径形成死锁。
//
// 只读路径不调这个：打开一次对话框就把年度行锁住，是任何人都不会预期的副作用。
std::optional<ComponentInventoryRepository::ConfirmedRevisionRef> resolve_confirmed_revision_ref(
    const drogon::orm::DbClientPtr& client,
    const std::string& bridge_id,
    const std::optional<std::string>& locked_revision_id) {
    return ComponentInventoryRepository(client).resolve_confirmed_revision_ref(
        bridge_id, locked_revision_id);
}

BindingOutcome revision_changed_outcome() {
    BindingOutcome outcome{BindingStatus::Conflict};
    outcome.error_code = "component_inventory_revision_changed";
    outcome.error_message = "构件台账版本已变化，请刷新后重试。";
    return outcome;
}

}  // namespace

ImportBindingRepository::ImportBindingRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

BindingOutcome ImportBindingRepository::bind_rating_tree(
    const std::string& import_id,
    const std::string& rating_tree_version_id,
    const std::string& actor_user_id,
    const std::string& expected_revision_id,
    const std::optional<EditLockCredentials>& edit_lock) {
    if (import_id.empty() || rating_tree_version_id.empty() ||
        actor_user_id.empty()) {
        return {BindingStatus::Invalid};
    }

    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&]() {
        if (tx) {
            try { tx->rollback(); } catch (...) {}
        }
    };
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto context = tx->execSqlSync(
            "select ir.bridge_id::text as bridge_id,"
            "ir.inspection_year_id::text as inspection_year_id,"
            "ir.import_status,iy.status as inspection_year_status,"
            "iy.component_inventory_revision_id::text as inventory_revision_id,"
            "profile.rating_tree_version_id::text as current_tree_version_id,"
            "profile.technical_condition_package_id::text as current_technical_package_id,"
            "coalesce(ir.parsed_result_json::text,'{}') as parsed "
            "from import_records ir "
            "left join inspection_years iy on iy.id=ir.inspection_year_id "
            "left join project_standard_profiles profile "
            "on profile.id=iy.standard_profile_id "
            "where ir.id=$1::uuid for update of ir",
            import_id);
        if (context.empty()) {
            rollback();
            return {BindingStatus::NotFound};
        }
        if (!edit_lock_still_active(tx, import_id, edit_lock)) {
            rollback();
            return {BindingStatus::EditLockInvalid};
        }
        const auto year_id =
            optional_row_text(context[0], "inspection_year_id");
        if (!year_id.has_value() ||
            context[0]["import_status"].as<std::string>() != "待校对" ||
            context[0]["inspection_year_status"].as<std::string>() !=
                "待校对") {
            rollback();
            return {BindingStatus::Conflict};
        }
        const auto locked_year = tx->execSqlSync(
            "select id from inspection_years "
            "where id=$1::uuid and status='待校对' for update",
            *year_id);
        if (locked_year.empty()) {
            rollback();
            return {BindingStatus::Conflict};
        }

        const auto target = tx->execSqlSync(
            "select tree.id::text as tree_id,tree.tree_name,"
            "tree.package_version as tree_package_version,"
            "tree.technical_condition_package_id::text as technical_package_id,"
            "tree.maintenance_package_id::text as maintenance_package_id,"
            "technical.package_version as h21_package_version,"
            "maintenance.package_version as maintenance_package_version "
            "from rating_tree_versions tree "
            "join standard_packages technical "
            "on technical.id=tree.technical_condition_package_id "
            "join standard_packages maintenance "
            "on maintenance.id=tree.maintenance_package_id "
            "where tree.id=$1::uuid and tree.status='published' "
            "and technical.is_enabled and technical.sync_status='正常' "
            "and maintenance.is_enabled and maintenance.sync_status='正常'",
            rating_tree_version_id);
        if (target.empty()) {
            const auto exists = tx->execSqlSync(
                "select 1 from rating_tree_versions where id=$1::uuid",
                rating_tree_version_id);
            rollback();
            return {exists.empty() ? BindingStatus::TreeNotFound
                                   : BindingStatus::TreeUnavailable};
        }

        const auto current_tree =
            optional_row_text(context[0], "current_tree_version_id");
        if (current_tree.has_value() &&
            *current_tree == rating_tree_version_id) {
            tx.reset();
            if (!latch->wait()) return {BindingStatus::Failed};
            return {BindingStatus::Ok};
        }

        const auto successful_formal = tx->execSqlSync(
            "select 1 from assessment_runs "
            "where inspection_year_id=$1::uuid and run_kind='正式' "
            "and result_status='成功' limit 1",
            *year_id);
        if (!successful_formal.empty()) {
            rollback();
            return {BindingStatus::Conflict};
        }

        const auto bridge_id = context[0]["bridge_id"].as<std::string>();
        const auto locked_revision_id =
            optional_row_text(context[0], "inventory_revision_id");
        const auto source_revision =
            resolve_confirmed_revision_ref(tx, bridge_id, locked_revision_id);
        if (!source_revision.has_value()) {
            rollback();
            return {BindingStatus::Conflict};
        }
        // 迁移的起点必须是用户看到的那份台账，否则派生出来的新版本基线就不对了。
        if (source_revision->id != expected_revision_id) {
            rollback(); return revision_changed_outcome();
        }

        const auto target_technical_package_id =
            target[0]["technical_package_id"].as<std::string>();
        std::string target_revision_id = source_revision->id;
        const auto target_mapping_complete = tx->execSqlSync(
            "select not exists ("
            "select 1 from bridge_component_inventory_entries entry "
            "where entry.inventory_revision_id=$1::uuid and entry.is_active "
            "and not exists ("
            "select 1 from bridge_component_standard_mappings mapping "
            "where mapping.inventory_entry_id=entry.id "
            "and mapping.standard_package_id=$2::uuid "
            "and mapping.is_active and mapping.confirmation_status='已确认'"
            ")) as complete",
            source_revision->id, target_technical_package_id);
        const bool can_reuse_inventory =
            !target_mapping_complete.empty() &&
            target_mapping_complete[0]["complete"].as<bool>();

        if (!can_reuse_inventory) {
            const auto current_technical_package_id =
                optional_row_text(context[0], "current_technical_package_id");
            bool source_complete = false;
            if (current_technical_package_id.has_value()) {
                const auto source_mapping_complete = tx->execSqlSync(
                    "select not exists ("
                    "select 1 from bridge_component_inventory_entries entry "
                    "where entry.inventory_revision_id=$1::uuid and entry.is_active "
                    "and not exists ("
                    "select 1 from bridge_component_standard_mappings mapping "
                    "where mapping.inventory_entry_id=entry.id "
                    "and mapping.standard_package_id=$2::uuid "
                    "and mapping.is_active and mapping.confirmation_status='已确认'"
                    ")) as complete",
                    source_revision->id, *current_technical_package_id);
                source_complete = !source_mapping_complete.empty() &&
                    source_mapping_complete[0]["complete"].as<bool>();
            } else {
                const auto source_mapping_complete = tx->execSqlSync(
                    "select not exists ("
                    "select 1 from bridge_component_inventory_entries entry "
                    "where entry.inventory_revision_id=$1::uuid and entry.is_active "
                    "and not exists ("
                    "select 1 from bridge_component_standard_mappings mapping "
                    "where mapping.inventory_entry_id=entry.id "
                    "and mapping.is_active and mapping.confirmation_status='已确认'"
                    ")) as complete",
                    source_revision->id);
                source_complete = !source_mapping_complete.empty() &&
                    source_mapping_complete[0]["complete"].as<bool>();
            }
            if (!source_complete) {
                rollback();
                return {BindingStatus::MappingIncompatible};
            }

            const auto new_revision = tx->execSqlSync(
                "insert into bridge_component_inventory_revisions ("
                "bridge_id,revision_number,baseline_revision_id,created_by_user_id"
                ") select $1::uuid,coalesce(max(revision_number),0)+1,"
                "$2::uuid,$3::uuid "
                "from bridge_component_inventory_revisions "
                "where bridge_id=$1::uuid returning id::text",
                bridge_id, source_revision->id, actor_user_id);
            if (new_revision.empty()) {
                rollback();
                return {BindingStatus::Failed};
            }
            target_revision_id =
                new_revision[0]["id"].as<std::string>();

            tx->execSqlSync(
                "insert into bridge_component_inventory_entries ("
                "inventory_revision_id,bridge_component_id,generation_batch_id,"
                "component_number,site_name,site_component_type,span_or_location,"
                "is_active,deactivated_at,deactivation_reason,sort_order,remarks"
                ") select $2::uuid,bridge_component_id,generation_batch_id,"
                "component_number,site_name,site_component_type,span_or_location,"
                "is_active,deactivated_at,deactivation_reason,sort_order,remarks "
                "from bridge_component_inventory_entries "
                "where inventory_revision_id=$1::uuid",
                source_revision->id, target_revision_id);

            if (current_technical_package_id.has_value()) {
                tx->execSqlSync(
                    "insert into bridge_component_standard_mappings ("
                    "inventory_entry_id,standard_package_id,"
                    "standard_bridge_type_id,standard_component_category_id,"
                    "structure_part,mapping_source,confirmation_status,"
                    "confirmed_by_user_id,confirmed_at,is_active"
                    ") select new_entry.id,$3::uuid,"
                    "source_mapping.standard_bridge_type_id,"
                    "source_mapping.standard_component_category_id,"
                    "source_mapping.structure_part,'评定树切换继承','已确认',"
                    "$4::uuid,now(),source_mapping.is_active "
                    "from bridge_component_inventory_entries old_entry "
                    "join bridge_component_inventory_entries new_entry "
                    "on new_entry.inventory_revision_id=$2::uuid "
                    "and new_entry.bridge_component_id=old_entry.bridge_component_id "
                    "join lateral ("
                    "select mapping.* "
                    "from bridge_component_standard_mappings mapping "
                    "where mapping.inventory_entry_id=old_entry.id "
                    "and mapping.standard_package_id=$5::uuid "
                    "and mapping.is_active "
                    "and mapping.confirmation_status='已确认' "
                    "order by mapping.created_at desc,mapping.id limit 1"
                    ") source_mapping on true "
                    "where old_entry.inventory_revision_id=$1::uuid",
                    source_revision->id, target_revision_id,
                    target_technical_package_id, actor_user_id,
                    *current_technical_package_id);
            } else {
                tx->execSqlSync(
                    "insert into bridge_component_standard_mappings ("
                    "inventory_entry_id,standard_package_id,"
                    "standard_bridge_type_id,standard_component_category_id,"
                    "structure_part,mapping_source,confirmation_status,"
                    "confirmed_by_user_id,confirmed_at,is_active"
                    ") select new_entry.id,$3::uuid,"
                    "source_mapping.standard_bridge_type_id,"
                    "source_mapping.standard_component_category_id,"
                    "source_mapping.structure_part,'评定树绑定继承','已确认',"
                    "$4::uuid,now(),source_mapping.is_active "
                    "from bridge_component_inventory_entries old_entry "
                    "join bridge_component_inventory_entries new_entry "
                    "on new_entry.inventory_revision_id=$2::uuid "
                    "and new_entry.bridge_component_id=old_entry.bridge_component_id "
                    "join lateral ("
                    "select mapping.* "
                    "from bridge_component_standard_mappings mapping "
                    "where mapping.inventory_entry_id=old_entry.id "
                    "and mapping.is_active "
                    "and mapping.confirmation_status='已确认' "
                    "order by mapping.created_at desc,mapping.id limit 1"
                    ") source_mapping on true "
                    "where old_entry.inventory_revision_id=$1::uuid",
                    source_revision->id, target_revision_id,
                    target_technical_package_id, actor_user_id);
            }

            const auto inherited_complete = tx->execSqlSync(
                "select not exists ("
                "select 1 from bridge_component_inventory_entries entry "
                "where entry.inventory_revision_id=$1::uuid and entry.is_active "
                "and not exists ("
                "select 1 from bridge_component_standard_mappings mapping "
                "where mapping.inventory_entry_id=entry.id "
                "and mapping.standard_package_id=$2::uuid "
                "and mapping.is_active and mapping.confirmation_status='已确认'"
                ")) as complete",
                target_revision_id, target_technical_package_id);
            if (inherited_complete.empty() ||
                !inherited_complete[0]["complete"].as<bool>()) {
                rollback();
                return {BindingStatus::MappingIncompatible};
            }
            tx->execSqlSync(
                "update bridge_component_inventory_revisions set "
                "status='已确认',confirmed_by_user_id=$2::uuid,"
                "confirmed_at=now(),confirmation_note="
                "'绑定评定树：继承原台账构件及已确认规范映射',updated_at=now() "
                "where id=$1::uuid",
                target_revision_id, actor_user_id);
        }

        auto profiles = tx->execSqlSync(
            "select id::text as id from project_standard_profiles "
            "where rating_tree_version_id=$1::uuid and status='生效' "
            "order by created_at,id limit 1",
            rating_tree_version_id);
        std::string profile_id;
        if (profiles.empty()) {
            profiles = tx->execSqlSync(
                "insert into project_standard_profiles ("
                "technical_condition_package_id,maintenance_package_id,"
                "rating_tree_version_id,created_by_user_id,change_reason"
                ") values($1::uuid,$2::uuid,$3::uuid,$4::uuid,"
                "'在构件绑定页面绑定评定树') returning id::text as id",
                target_technical_package_id,
                target[0]["maintenance_package_id"].as<std::string>(),
                rating_tree_version_id, actor_user_id);
        }
        if (profiles.empty()) {
            rollback();
            return {BindingStatus::Failed};
        }
        profile_id = profiles[0]["id"].as<std::string>();

        Json::Value parsed;
        if (!parse_json(context[0]["parsed"].as<std::string>(), parsed)) {
            rollback();
            return {BindingStatus::Invalid};
        }
        const Json::Value stored = parsed;
        if (parsed["defects"].isArray()) {
            for (auto& defect : parsed["defects"]) {
                defect["rating_tree_node_id"] =
                    Json::Value(Json::nullValue);
                defect["standard_defect_indicator_id"] =
                    Json::Value(Json::nullValue);
                defect["rating_tree_match_method"] =
                    Json::Value(Json::nullValue);
                defect["rating_tree_match_evidence"] =
                    Json::Value(Json::nullValue);
                if (defect["bridge_component_id"].isString() &&
                    !defect["bridge_component_id"].asString().empty()) {
                    defect["component_inventory_revision_id"] =
                        target_revision_id;
                }
            }
        }

        const auto tree =
            RatingTreeRepository(tx).load_published_tree(
                rating_tree_version_id);
        const auto target_revision =
            ComponentInventoryRepository(tx).get_revision(
                target_revision_id);
        if (!tree.has_value() || !target_revision.has_value() ||
            !review::normalize_defect_rating_tree_associations(
                 parsed, stored, rating_tree_version_id,
                 target_technical_package_id, *tree,
                 target_revision).ok) {
            rollback();
            return {BindingStatus::MappingIncompatible};
        }

        tx->execSqlSync(
            "update inspection_years set standard_profile_id=$2::uuid,"
            "component_inventory_revision_id=$3::uuid,updated_at=now() "
            "where id=$1::uuid and status='待校对'",
            *year_id, profile_id, target_revision_id);
        tx->execSqlSync(
            "update import_records set parsed_result_json=$2::jsonb "
            "where id=$1::uuid",
            import_id, compact_json(parsed));

        tx.reset();
        if (!latch->wait()) return {BindingStatus::Failed};
        // 概览不再由这里产出：调用方绑完会重取解析工作区，那是当前状态的唯一来源。
        return {BindingStatus::Ok};
    } catch (...) {
        rollback();
        return {BindingStatus::Failed};
    }
}

}  // namespace bridge_report::db

