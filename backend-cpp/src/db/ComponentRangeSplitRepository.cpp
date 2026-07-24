#include "bridge_report/db/ComponentRangeSplitRepository.hpp"

#include <memory>
#include <utility>

#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/inventory/ComponentInventoryModels.hpp"

namespace bridge_report::db {
namespace {

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

std::string make_token(
    const std::string& import_id,
    const Json::Value& parsed,
    const inventory::InventoryRevision& revision,
    const std::vector<review::ComponentRangeSplitTarget>& targets,
    const review::ComponentRangeSplitPlan& plan) {
    Json::Value canonical(Json::objectValue);
    canonical["import_id"] = import_id;
    canonical["parsed_result"] = parsed;
    canonical["inventory_revision"] = inventory::inventory_revision_json(revision);
    canonical["targets"] = Json::Value(Json::arrayValue);
    for (const auto& target : targets) {
        Json::Value item(Json::objectValue);
        item["part_name"] = target.part_name;
        item["component_number"] = target.component_number;
        canonical["targets"].append(std::move(item));
    }
    canonical["planned_result"] = plan.result_json;
    return "sha256:" + auth::sha256_hex(compact_json(canonical));
}

ComponentRangeSplitOutcome plan_outcome(
    const std::string& import_id,
    const Json::Value& parsed,
    const inventory::InventoryRevision& revision,
    const std::vector<review::ComponentRangeSplitTarget>& targets) {
    auto plan = review::plan_component_range_splits(parsed, revision, targets);
    ComponentRangeSplitOutcome outcome;
    outcome.status = outcome_status(plan.status);
    outcome.error_code = plan.error_code;
    outcome.error_message = plan.error_message;
    outcome.rejected_target = plan.rejected_target;
    if (plan.status == review::ComponentRangeSplitPlanStatus::Ok) {
        outcome.impact_token = make_token(import_id, parsed, revision, targets, plan);
    }
    outcome.plan = std::move(plan);
    return outcome;
}

void materialize_origin(
    Json::Value& result, const std::string& operation_id,
    const std::string& user_id, const std::string& operated_at) {
    if (!result["defects"].isArray()) return;
    for (auto& defect : result["defects"]) {
        auto& origin = defect["range_split_origin"];
        if (!origin.isObject()
            || origin["operation_id"].asString() != "__range_split_operation__") continue;
        origin["operation_id"] = operation_id;
        origin["operated_by_user_id"] = user_id;
        origin["operated_at"] = operated_at;
    }
}

}  // namespace

ComponentRangeSplitRepository::ComponentRangeSplitRepository(
    drogon::orm::DbClientPtr db_client) : db_client_(std::move(db_client)) {}

ComponentRangeSplitOutcome ComponentRangeSplitRepository::preview(
    const std::string& import_id,
    const std::vector<review::ComponentRangeSplitTarget>& targets) {
    try {
        const auto rows = db_client_->execSqlSync(
            "select bridge_id::text as bridge_id,import_status,"
            "coalesce(parsed_result_json::text,'{}') as parsed "
            "from import_records where id=$1::uuid", import_id);
        if (rows.empty()) return {ComponentRangeSplitStatus::NotFound};
        if (rows[0]["import_status"].as<std::string>() != "待校对") {
            return {ComponentRangeSplitStatus::Conflict};
        }
        Json::Value parsed;
        if (!parse_json(rows[0]["parsed"].as<std::string>(), parsed)) {
            return {ComponentRangeSplitStatus::Failed};
        }
        const auto revision = ComponentInventoryRepository(db_client_)
            .get_latest_revision(rows[0]["bridge_id"].as<std::string>());
        if (!revision || !(revision->status == "已确认"
                           || revision->status == "confirmed")) {
            return {ComponentRangeSplitStatus::Conflict};
        }
        return plan_outcome(import_id, parsed, *revision, targets);
    } catch (...) {
        return {ComponentRangeSplitStatus::Failed};
    }
}

ComponentRangeSplitOutcome ComponentRangeSplitRepository::apply(
    const std::string& import_id,
    const std::vector<review::ComponentRangeSplitTarget>& targets,
    const std::string& expected_impact_token,
    const std::string& user_id) {
    const auto latch = std::make_shared<CommitLatch>();
    std::shared_ptr<drogon::orm::Transaction> tx;
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto rows = tx->execSqlSync(
            "select bridge_id::text as bridge_id,import_status,"
            "coalesce(parsed_result_json::text,'{}') as parsed "
            "from import_records where id=$1::uuid for update", import_id);
        if (rows.empty()) { tx->rollback(); return {ComponentRangeSplitStatus::NotFound}; }
        if (rows[0]["import_status"].as<std::string>() != "待校对") {
            tx->rollback(); return {ComponentRangeSplitStatus::Conflict};
        }
        Json::Value parsed;
        if (!parse_json(rows[0]["parsed"].as<std::string>(), parsed)) {
            tx->rollback(); return {ComponentRangeSplitStatus::Failed};
        }
        const auto revision = ComponentInventoryRepository(tx).get_latest_revision(
            rows[0]["bridge_id"].as<std::string>());
        if (!revision || !(revision->status == "已确认"
                           || revision->status == "confirmed")) {
            tx->rollback(); return {ComponentRangeSplitStatus::Conflict};
        }
        auto outcome = plan_outcome(import_id, parsed, *revision, targets);
        if (outcome.status != ComponentRangeSplitStatus::Ok) {
            tx->rollback();
            return outcome;
        }
        if (outcome.impact_token != expected_impact_token) {
            tx->rollback();
            outcome.status = ComponentRangeSplitStatus::Stale;
            outcome.error_code = "component_range_split_stale";
            outcome.error_message = "拆分预览已过期，请重新预览后再应用。";
            return outcome;
        }
        const auto metadata = tx->execSqlSync(
            "select gen_random_uuid()::text as operation_id,"
            "to_char(clock_timestamp() at time zone 'UTC',"
            "'YYYY-MM-DD\"T\"HH24:MI:SS.MS\"Z\"') as operated_at");
        outcome.operation_id = metadata[0]["operation_id"].as<std::string>();
        materialize_origin(
            outcome.plan->result_json, outcome.operation_id, user_id,
            metadata[0]["operated_at"].as<std::string>());
        tx->execSqlSync(
            "update import_records set parsed_result_json=$2::jsonb,updated_at=now() "
            "where id=$1::uuid", import_id, compact_json(outcome.plan->result_json));
        tx.reset();
        if (!latch->wait()) return {ComponentRangeSplitStatus::Failed};
        const auto overview = ImportBindingRepository(db_client_).overview(import_id);
        if (overview.status == BindingStatus::Ok) outcome.overview = overview.overview;
        return outcome;
    } catch (...) {
        if (tx) tx->rollback();
        return {ComponentRangeSplitStatus::Failed};
    }
}

}  // namespace bridge_report::db
