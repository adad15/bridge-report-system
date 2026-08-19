#include "bridge_report/db/ComponentRangeSplitRepository.hpp"

#include "bridge_report/auth/PasswordHash.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <trantor/utils/Logger.h>

#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/inventory/ComponentMatcher.hpp"

namespace bridge_report::db {
namespace {

using Clock = std::chrono::steady_clock;

long long elapsed_ms(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start)
        .count();
}

std::optional<std::string> optional_row_text(
    const drogon::orm::Row& row, const char* column) {
    return row[column].isNull()
        ? std::nullopt
        : std::optional<std::string>(row[column].as<std::string>());
}

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

bool parse_json(const std::string& text, Json::Value& output) {
    Json::CharReaderBuilder builder;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.data(), text.data() + text.size(), &output, &errors);
}

ComponentRangeSplitStatus outcome_status(
    review::ComponentRangeSplitPlanStatus status) {
    return status == review::ComponentRangeSplitPlanStatus::Ok
        ? ComponentRangeSplitStatus::Ok : ComponentRangeSplitStatus::Invalid;
}

Json::Value analysis_summary_json(
    const review::ComponentRangeSplitAnalysis& analysis) {
    Json::Value value(Json::objectValue);
    value["items"] = Json::Value(Json::arrayValue);
    for (const auto& item : analysis.items) {
        Json::Value row(Json::objectValue);
        row["part_name"] = item.target.part_name;
        row["component_number"] = item.target.component_number;
        row["expanded_component_count"] = item.expanded_component_count;
        row["source_defect_count"] = item.source_defect_count;
        row["result_defect_count"] = item.result_defect_count;
        row["result_photo_count"] = item.result_photo_count;
        row["bound_count"] = item.bound_count;
        row["ambiguous_count"] = item.ambiguous_count;
        row["unmatched_count"] = item.unmatched_count;
        value["items"].append(std::move(row));
    }
    const auto& totals = analysis.totals;
    value["totals"]["selected_range_count"] = totals.selected_range_count;
    value["totals"]["source_defect_count"] = totals.source_defect_count;
    value["totals"]["result_defect_count"] = totals.result_defect_count;
    value["totals"]["result_photo_count"] = totals.result_photo_count;
    value["totals"]["bound_count"] = totals.bound_count;
    value["totals"]["ambiguous_count"] = totals.ambiguous_count;
    value["totals"]["unmatched_count"] = totals.unmatched_count;
    return value;
}

std::string make_token(
    const std::string& import_id,
    const Json::Value& parsed,
    const inventory::InventoryRevision& revision,
    const std::vector<review::ComponentRangeSplitTarget>& targets,
    const review::ComponentRangeSplitAnalysis& analysis) {
    Json::Value canonical(Json::objectValue);
    canonical["import_id"] = import_id;
    canonical["parsed_result_digest"] =
        "sha256:" + auth::sha256_hex(compact_json(parsed));
    canonical["inventory_revision_id"] = revision.id;
    auto canonical_targets = targets;
    std::sort(canonical_targets.begin(), canonical_targets.end(), [](const auto& left, const auto& right) {
        const auto left_number = inventory::normalize_component_number(left.component_number);
        const auto right_number = inventory::normalize_component_number(right.component_number);
        return std::tie(left.part_name, left_number, left.component_number)
            < std::tie(right.part_name, right_number, right.component_number);
    });
    canonical_targets.erase(std::unique(
        canonical_targets.begin(), canonical_targets.end(), [](const auto& left, const auto& right) {
            return left.part_name == right.part_name
                && inventory::normalize_component_number(left.component_number)
                    == inventory::normalize_component_number(right.component_number);
        }), canonical_targets.end());
    canonical["targets"] = Json::Value(Json::arrayValue);
    for (const auto& target : canonical_targets) {
        Json::Value item(Json::objectValue);
        item["part_name"] = target.part_name;
        item["component_number"] =
            inventory::normalize_component_number(target.component_number);
        canonical["targets"].append(std::move(item));
    }
    canonical["analysis"] = analysis_summary_json(analysis);
    return "sha256:" + auth::sha256_hex(compact_json(canonical));
}

ComponentRangeSplitOutcome analysis_outcome(
    const Json::Value& parsed,
    const inventory::InventoryRevision& revision,
    const std::vector<review::ComponentRangeSplitTarget>& targets) {
    auto analysis = review::analyze_component_range_splits(parsed, revision, targets);
    ComponentRangeSplitOutcome outcome;
    outcome.status = outcome_status(analysis.status);
    outcome.error_code = analysis.error_code;
    outcome.error_message = analysis.error_message;
    outcome.rejected_target = analysis.rejected_target;
    outcome.analysis = std::move(analysis);
    return outcome;
}

void materialize_origin(
    Json::Value& result,
    const std::string& operation_id,
    const std::string& user_id,
    const std::string& operated_at) {
    if (!result["defects"].isArray()) return;
    for (auto& defect : result["defects"]) {
        auto& origin = defect["range_split_origin"];
        if (!origin.isObject()
            || origin["operation_id"].asString() != "__range_split_operation__") {
            continue;
        }
        origin["operation_id"] = operation_id;
        origin["operated_by_user_id"] = user_id;
        origin["operated_at"] = operated_at;
    }
}

void log_timing(
    const char* operation,
    const std::string& import_id,
    const review::ComponentRangeSplitAnalysis* analysis,
    long long load_import_ms,
    long long load_inventory_ms,
    long long analyze_ms,
    long long token_ms,
    long long materialize_ms,
    long long persist_ms,
    long long total_ms) {
    LOG_INFO << "component range split " << operation
             << " import=" << import_id
             << " selected=" << (analysis ? analysis->totals.selected_range_count : 0)
             << " source_defects=" << (analysis ? analysis->totals.source_defect_count : 0)
             << " result_defects=" << (analysis ? analysis->totals.result_defect_count : 0)
             << " result_photos=" << (analysis ? analysis->totals.result_photo_count : 0)
             << " load_import_ms=" << load_import_ms
             << " load_inventory_ms=" << load_inventory_ms
             << " analyze_ms=" << analyze_ms
             << " token_ms=" << token_ms
             << " materialize_ms=" << materialize_ms
             << " persist_ms=" << persist_ms
             << " total_ms=" << total_ms;
}

// 与绑定那条路径返回同一个错误码，前端才能用同一套处置逻辑接住三条路径。
ComponentRangeSplitOutcome revision_changed_split_outcome() {
    ComponentRangeSplitOutcome outcome{ComponentRangeSplitStatus::Conflict};
    outcome.error_code = "component_inventory_revision_changed";
    outcome.error_message = "构件台账版本已变化，请刷新后重试。";
    return outcome;
}

}  // namespace

ComponentRangeSplitRepository::ComponentRangeSplitRepository(
    drogon::orm::DbClientPtr db_client) : db_client_(std::move(db_client)) {}

ComponentRangeSplitOutcome ComponentRangeSplitRepository::preview(
    const std::string& import_id,
    const std::vector<review::ComponentRangeSplitTarget>& targets,
    const std::string& expected_revision_id) {
    const auto total_start = Clock::now();
    long long load_import_ms = 0;
    long long load_inventory_ms = 0;
    long long analyze_ms = 0;
    long long token_ms = 0;
    try {
        auto stage_start = Clock::now();
        const auto rows = db_client_->execSqlSync(
            "select ir.bridge_id::text as bridge_id,ir.import_status,"
            "iy.component_inventory_revision_id::text as inventory_revision_id,"
            "coalesce(ir.parsed_result_json::text,'{}') as parsed "
            "from import_records ir "
            "left join inspection_years iy on iy.id=ir.inspection_year_id "
            "where ir.id=$1::uuid", import_id);
        load_import_ms = elapsed_ms(stage_start);
        if (rows.empty()) return {ComponentRangeSplitStatus::NotFound};
        if (rows[0]["import_status"].as<std::string>() != "待校对") {
            return {ComponentRangeSplitStatus::Conflict};
        }
        Json::Value parsed;
        if (!parse_json(rows[0]["parsed"].as<std::string>(), parsed)) {
            return {ComponentRangeSplitStatus::Failed};
        }
        stage_start = Clock::now();
        // 必须跟绑定校验用同一个版本：年度锁定的优先，否则取最新已确认版本。
        const auto revision = ComponentInventoryRepository(db_client_)
            .resolve_confirmed_revision(
                rows[0]["bridge_id"].as<std::string>(),
                optional_row_text(rows[0], "inventory_revision_id"));
        load_inventory_ms = elapsed_ms(stage_start);
        if (!revision) return {ComponentRangeSplitStatus::Conflict};
        // 只校验，不锁定——预览是读操作。
        if (revision->id != expected_revision_id) return revision_changed_split_outcome();
        stage_start = Clock::now();
        auto outcome = analysis_outcome(parsed, *revision, targets);
        analyze_ms = elapsed_ms(stage_start);
        if (outcome.status == ComponentRangeSplitStatus::Ok) {
            stage_start = Clock::now();
            outcome.impact_token = make_token(
                import_id, parsed, *revision, targets, *outcome.analysis);
            token_ms = elapsed_ms(stage_start);
        }
        log_timing("preview", import_id, outcome.analysis ? &*outcome.analysis : nullptr,
                   load_import_ms, load_inventory_ms, analyze_ms, token_ms, 0, 0,
                   elapsed_ms(total_start));
        return outcome;
    } catch (...) {
        log_timing("preview_failed", import_id, nullptr, load_import_ms,
                   load_inventory_ms, analyze_ms, token_ms, 0, 0,
                   elapsed_ms(total_start));
        return {ComponentRangeSplitStatus::Failed};
    }
}

ComponentRangeSplitOutcome ComponentRangeSplitRepository::apply(
    const std::string& import_id,
    const std::vector<review::ComponentRangeSplitTarget>& targets,
    const std::string& expected_impact_token,
    const std::string& user_id,
    const std::string& expected_revision_id,
    const std::optional<EditLockCredentials>& edit_lock) {
    const auto total_start = Clock::now();
    long long load_import_ms = 0;
    long long load_inventory_ms = 0;
    long long analyze_ms = 0;
    long long materialize_ms = 0;
    long long persist_ms = 0;
    const auto latch = std::make_shared<CommitLatch>();
    std::shared_ptr<drogon::orm::Transaction> tx;
    try {
        tx = db_client_->newTransaction(latch->callback());
        auto stage_start = Clock::now();
        const auto rows = tx->execSqlSync(
            "select ir.bridge_id::text as bridge_id,ir.import_status,"
            "ir.inspection_year_id::text as inspection_year_id,"
            "iy.component_inventory_revision_id::text as inventory_revision_id,"
            "coalesce(ir.parsed_result_json::text,'{}') as parsed "
            "from import_records ir "
            "left join inspection_years iy on iy.id=ir.inspection_year_id "
            "where ir.id=$1::uuid for update of ir", import_id);
        load_import_ms = elapsed_ms(stage_start);
        if (rows.empty()) { tx->rollback(); return {ComponentRangeSplitStatus::NotFound}; }
        if (rows[0]["import_status"].as<std::string>() != "待校对") {
            tx->rollback(); return {ComponentRangeSplitStatus::Conflict};
        }
        // 与绑定写接口同一套：路由外层拦一道，这里在写事务内复查。
        if (edit_lock.has_value()) {
            const auto active = tx->execSqlSync(
                "select exists(select 1 from import_record_edit_locks "
                "where import_record_id=$1::uuid and user_id=$2::uuid "
                "and user_session_id=$3::uuid and lock_token_hash=$4 "
                "and expires_at>now()) as active",
                import_id, edit_lock->user_id, edit_lock->session_id,
                auth::sha256_hex(edit_lock->lock_token));
            if (active.empty() || !active[0]["active"].as<bool>()) {
                tx->rollback();
                return {ComponentRangeSplitStatus::EditLockInvalid};
            }
        }
        Json::Value parsed;
        if (!parse_json(rows[0]["parsed"].as<std::string>(), parsed)) {
            tx->rollback(); return {ComponentRangeSplitStatus::Failed};
        }
        stage_start = Clock::now();
        const auto bridge_id = rows[0]["bridge_id"].as<std::string>();
        const auto year_id = optional_row_text(rows[0], "inspection_year_id");
        // 锁顺序 import_records -> inspection_years：联查那一份是在没有年度行锁的
        // 情况下读的，而两条导入记录可以共享同一个年度。写路径必须在年度行锁内重读。
        std::optional<std::string> locked_revision_id;
        if (year_id.has_value()) {
            const auto year_row = tx->execSqlSync(
                "select component_inventory_revision_id::text as inventory_revision_id "
                "from inspection_years where id=$1::uuid for update",
                *year_id);
            if (!year_row.empty()) {
                locked_revision_id = optional_row_text(year_row[0], "inventory_revision_id");
            }
        }
        const auto revision = ComponentInventoryRepository(tx).resolve_confirmed_revision(
            bridge_id, locked_revision_id);
        load_inventory_ms = elapsed_ms(stage_start);
        if (!revision) { tx->rollback(); return {ComponentRangeSplitStatus::Conflict}; }
        if (revision->id != expected_revision_id) {
            tx->rollback(); return revision_changed_split_outcome();
        }
        // 应用会把构件绑进病害，所以要跟其他写操作一样把版本锁进年度；否则随后一次
        // 绑定可能把年度锁到别的版本上，而这批病害已经按当前版本绑好了。
        if (!ComponentInventoryRepository(tx).lock_pending_year_revision(
                year_id, bridge_id,
                locked_revision_id, revision->id)) {
            tx->rollback(); return {ComponentRangeSplitStatus::Conflict};
        }
        stage_start = Clock::now();
        auto outcome = analysis_outcome(parsed, *revision, targets);
        analyze_ms = elapsed_ms(stage_start);
        if (outcome.status != ComponentRangeSplitStatus::Ok) {
            tx->rollback();
            return outcome;
        }
        stage_start = Clock::now();
        outcome.impact_token = make_token(
            import_id, parsed, *revision, targets, *outcome.analysis);
        const auto token_ms = elapsed_ms(stage_start);
        if (outcome.impact_token != expected_impact_token) {
            tx->rollback();
            outcome.status = ComponentRangeSplitStatus::Stale;
            outcome.error_code = "component_range_split_stale";
            outcome.error_message = "拆分预览已过期，请重新预览后再应用。";
            return outcome;
        }
        stage_start = Clock::now();
        outcome.plan = review::materialize_component_range_splits(
            parsed, *outcome.analysis);
        materialize_ms = elapsed_ms(stage_start);
        const auto metadata = tx->execSqlSync(
            "select gen_random_uuid()::text as operation_id,"
            "to_char(clock_timestamp() at time zone 'UTC',"
            "'YYYY-MM-DD\"T\"HH24:MI:SS.MS\"Z\"') as operated_at");
        outcome.operation_id = metadata[0]["operation_id"].as<std::string>();
        materialize_origin(
            outcome.plan->result_json, outcome.operation_id, user_id,
            metadata[0]["operated_at"].as<std::string>());
        stage_start = Clock::now();
        tx->execSqlSync(
            "update import_records set parsed_result_json=$2::jsonb,updated_at=now() "
            "where id=$1::uuid", import_id, compact_json(outcome.plan->result_json));
        tx.reset();
        if (!latch->wait()) return {ComponentRangeSplitStatus::Failed};
        const auto overview = ImportBindingRepository(db_client_).overview(import_id);
        if (overview.status == BindingStatus::Ok) outcome.overview = overview.overview;
        persist_ms = elapsed_ms(stage_start);
        log_timing("apply", import_id, &*outcome.analysis,
                   load_import_ms, load_inventory_ms, analyze_ms, token_ms,
                   materialize_ms, persist_ms, elapsed_ms(total_start));
        return outcome;
    } catch (...) {
        if (tx) tx->rollback();
        log_timing("apply_failed", import_id, nullptr, load_import_ms,
                   load_inventory_ms, analyze_ms, 0, materialize_ms,
                   persist_ms, elapsed_ms(total_start));
        return {ComponentRangeSplitStatus::Failed};
    }
}

}  // namespace bridge_report::db
