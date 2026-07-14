#include "bridge_report/db/DefectThreadRepository.hpp"

#include <memory>
#include <utility>

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {

namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

struct LockedObservation {
    std::string id;
    std::string bridge_id;
    std::string bridge_component_id;
    std::optional<std::string> defect_thread_id;
    std::string review_status;
    std::string updated_at;
    bool year_is_current{false};
    std::string year_status;
};

// 步骤 1：FOR UPDATE 锁定观测并携带其年度版本状态。
std::optional<LockedObservation> lock_observation(const TransactionPtr& tx, const std::string& observation_id) {
    const auto rows = tx->execSqlSync(
        "select o.id::text as id, o.bridge_id::text as bridge_id, "
        "o.bridge_component_id::text as bridge_component_id, o.defect_thread_id::text as defect_thread_id, "
        "o.review_status, o.updated_at::text as updated_at, iy.is_current, iy.status as year_status "
        "from defect_observations o "
        "join inspection_years iy on iy.id = o.inspection_year_id "
        "where o.id = $1::uuid for update of o",
        observation_id
    );
    if (rows.empty()) {
        return std::nullopt;
    }
    const auto& row = rows[0];
    LockedObservation observation;
    observation.id = row["id"].as<std::string>();
    observation.bridge_id = row["bridge_id"].as<std::string>();
    observation.bridge_component_id = row["bridge_component_id"].as<std::string>();
    if (!row["defect_thread_id"].isNull()) {
        observation.defect_thread_id = row["defect_thread_id"].as<std::string>();
    }
    observation.review_status = row["review_status"].as<std::string>();
    observation.updated_at = row["updated_at"].as<std::string>();
    observation.year_is_current = row["is_current"].as<bool>();
    observation.year_status = row["year_status"].as<std::string>();
    return observation;
}

// 步骤 2/3：当前有效版本 + 正式状态 + 乐观令牌（模块 06 规格 §9.2-9.3）。
std::optional<std::pair<std::string, std::string>> validate_observation_state(
    const LockedObservation& observation,
    const std::string& expected_updated_at
) {
    if (!observation.year_is_current || observation.year_status != "已确认") {
        return std::make_pair(
            "observation_not_current", "该观测属于旧修订版或未确认的年度版本，不能修改线索绑定。");
    }
    if (observation.review_status != "已确认" && observation.review_status != "已修改") {
        return std::make_pair(
            "observation_not_formal",
            "该观测状态为「" + observation.review_status + "」，不是正式事实，不能绑定线索。");
    }
    if (observation.updated_at != expected_updated_at) {
        return std::make_pair(
            "observation_revision_conflict", "该观测已被其他操作更新，请刷新后重试。");
    }
    return std::nullopt;
}

// 步骤 6：已被模块 07 人工确认的对比引用的观测，须先撤销对比结论才能重绑。
bool referenced_by_confirmed_comparison(const TransactionPtr& tx, const std::string& observation_id) {
    const auto rows = tx->execSqlSync(
        "select 1 from defect_comparisons "
        "where (previous_defect_observation_id = $1::uuid or current_defect_observation_id = $1::uuid) "
        "and confirmation_status = '人工已确认' limit 1",
        observation_id
    );
    return !rows.empty();
}

// 步骤 8：按当前有效绑定重算线索首见/末见年份（无绑定时置空）。
void recompute_thread_span(const TransactionPtr& tx, const std::string& thread_id) {
    tx->execSqlSync(
        "with current_obs as ("
        "  select o.inspection_year_id, iy.inspection_year "
        "  from defect_observations o "
        "  join inspection_years iy on iy.id = o.inspection_year_id "
        "  where o.defect_thread_id = $1::uuid and iy.is_current and iy.status = '已确认' "
        "  and o.review_status in ('已确认', '已修改')"
        ") "
        "update defect_threads set "
        "first_seen_inspection_id = (select inspection_year_id from current_obs order by inspection_year asc limit 1), "
        "latest_seen_inspection_id = (select inspection_year_id from current_obs order by inspection_year desc limit 1), "
        "updated_at = now() "
        "where id = $1::uuid",
        thread_id
    );
}

// 步骤 7：写绑定并刷新乐观令牌；返回新 updated_at 作为下次请求的令牌。
std::string update_observation_binding(
    const TransactionPtr& tx,
    const std::string& observation_id,
    const std::optional<std::string>& thread_id
) {
    const auto rows = tx->execSqlSync(
        "update defect_observations set defect_thread_id = $2::uuid, updated_at = now() "
        "where id = $1::uuid returning updated_at::text as updated_at",
        observation_id,
        thread_id
    );
    return rows[0]["updated_at"].as<std::string>();
}

Json::Value thread_to_json(const TransactionPtr& tx, const std::string& thread_id) {
    const auto rows = tx->execSqlSync(
        "select t.id::text as id, t.system_number, t.thread_name, t.defect_type, t.defect_location, "
        "t.current_status, t.confirmation_status, "
        "fy.inspection_year as first_seen_year, ly.inspection_year as latest_seen_year "
        "from defect_threads t "
        "left join inspection_years fy on fy.id = t.first_seen_inspection_id "
        "left join inspection_years ly on ly.id = t.latest_seen_inspection_id "
        "where t.id = $1::uuid",
        thread_id
    );
    Json::Value thread;
    if (rows.empty()) {
        return thread;
    }
    const auto& row = rows[0];
    thread["id"] = row["id"].as<std::string>();
    thread["system_number"] = row["system_number"].as<std::string>();
    thread["thread_name"] = row["thread_name"].as<std::string>();
    thread["defect_type"] = row["defect_type"].as<std::string>();
    thread["defect_location"] =
        row["defect_location"].isNull() ? Json::Value(Json::nullValue) : Json::Value(row["defect_location"].as<std::string>());
    thread["current_status"] = row["current_status"].as<std::string>();
    thread["confirmation_status"] = row["confirmation_status"].as<std::string>();
    thread["first_seen_year"] =
        row["first_seen_year"].isNull() ? Json::Value(Json::nullValue) : Json::Value(row["first_seen_year"].as<int>());
    thread["latest_seen_year"] =
        row["latest_seen_year"].isNull() ? Json::Value(Json::nullValue) : Json::Value(row["latest_seen_year"].as<int>());
    return thread;
}

}  // namespace

DefectThreadRepository::DefectThreadRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

ThreadBindingOutcome DefectThreadRepository::create_thread(
    const std::string& bridge_component_id,
    const std::string& defect_type,
    const std::string& defect_location,
    const std::string& first_observation_id,
    const std::string& expected_observation_updated_at,
    const std::optional<std::string>& thread_name
) {
    std::shared_ptr<drogon::orm::Transaction> tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto fail = [&](std::string code, std::string message) -> ThreadBindingOutcome {
        if (tx != nullptr) {
            try { tx->rollback(); } catch (...) {}
        }
        ThreadBindingOutcome failed;
        failed.error_code = std::move(code);
        failed.error_message = std::move(message);
        return failed;
    };

    try {
        tx = db_client_->newTransaction(latch->callback());

        const auto observation = lock_observation(tx, first_observation_id);
        if (!observation.has_value()) {
            return fail("defect_observation_not_found", "指定的病害观测不存在。");
        }
        if (const auto issue = validate_observation_state(*observation, expected_observation_updated_at)) {
            return fail(issue->first, issue->second);
        }
        if (observation->bridge_component_id != bridge_component_id) {
            return fail("thread_component_mismatch", "首条观测不属于目标构件，线索必须与观测同构件。");
        }
        // 创建即首绑：已绑定其他线索的观测请先在绑定接口显式重绑或解绑。
        if (observation->defect_thread_id.has_value()) {
            return fail("observation_already_bound", "该观测已绑定其他线索，请先显式重新绑定或解绑。");
        }

        const std::string resolved_name = thread_name.has_value() && !thread_name->empty()
            ? *thread_name
            : defect_type + "｜" + defect_location;
        const auto inserted = tx->execSqlSync(
            "insert into defect_threads "
            "(bridge_id, bridge_component_id, thread_name, defect_type, defect_location, confirmation_status) "
            "values ($1::uuid, $2::uuid, $3, $4, $5, '人工已确认') returning id::text as id",
            observation->bridge_id,
            bridge_component_id,
            resolved_name,
            defect_type,
            defect_location
        );
        const auto thread_id = inserted[0]["id"].as<std::string>();

        const auto new_updated_at = update_observation_binding(tx, first_observation_id, thread_id);
        recompute_thread_span(tx, thread_id);

        ThreadBindingOutcome outcome;
        outcome.body["created"] = true;
        outcome.body["defect_thread"] = thread_to_json(tx, thread_id);
        outcome.body["observation_id"] = first_observation_id;
        outcome.body["observation_updated_at"] = new_updated_at;

        tx.reset();
        if (!latch->wait()) {
            return fail("database_commit_failed", "数据库提交失败。");
        }
        outcome.success = true;
        return outcome;
    } catch (const drogon::orm::DrogonDbException& exception) {
        return fail("db_write_failed", exception.base().what());
    } catch (const std::exception& exception) {
        return fail("db_write_failed", exception.what());
    }
}

ThreadBindingOutcome DefectThreadRepository::bind_observation(
    const std::string& observation_id,
    const std::optional<std::string>& defect_thread_id,
    const std::string& expected_observation_updated_at,
    bool confirm_rebind
) {
    std::shared_ptr<drogon::orm::Transaction> tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto fail = [&](std::string code, std::string message) -> ThreadBindingOutcome {
        if (tx != nullptr) {
            try { tx->rollback(); } catch (...) {}
        }
        ThreadBindingOutcome failed;
        failed.error_code = std::move(code);
        failed.error_message = std::move(message);
        return failed;
    };

    try {
        tx = db_client_->newTransaction(latch->callback());

        const auto observation = lock_observation(tx, observation_id);
        if (!observation.has_value()) {
            return fail("defect_observation_not_found", "指定的病害观测不存在。");
        }
        if (const auto issue = validate_observation_state(*observation, expected_observation_updated_at)) {
            return fail(issue->first, issue->second);
        }

        // 步骤 4：目标线索必须与观测同桥、同构件（跨构件绑定一律拒绝）。
        if (defect_thread_id.has_value()) {
            const auto thread_rows = tx->execSqlSync(
                "select bridge_id::text as bridge_id, bridge_component_id::text as bridge_component_id "
                "from defect_threads where id = $1::uuid for update",
                *defect_thread_id
            );
            if (thread_rows.empty()) {
                return fail("defect_thread_not_found", "指定的病害线索不存在。");
            }
            if (thread_rows[0]["bridge_id"].as<std::string>() != observation->bridge_id
                || thread_rows[0]["bridge_component_id"].as<std::string>() != observation->bridge_component_id) {
                return fail("thread_component_mismatch", "目标线索与观测不属于同一桥梁构件。");
            }
        }

        const auto& old_thread_id = observation->defect_thread_id;
        const bool changing = old_thread_id != defect_thread_id;
        if (!changing) {
            // 幂等：绑定关系没有变化，直接返回当前状态。
            ThreadBindingOutcome outcome;
            outcome.success = true;
            outcome.body["bound"] = defect_thread_id.has_value();
            outcome.body["defect_thread_id"] =
                defect_thread_id.has_value() ? Json::Value(*defect_thread_id) : Json::Value(Json::nullValue);
            outcome.body["observation_id"] = observation_id;
            outcome.body["observation_updated_at"] = observation->updated_at;
            tx.reset();
            latch->wait();
            return outcome;
        }

        // 步骤 5：改变既有绑定（换线索或解绑）必须显式确认。
        if (old_thread_id.has_value() && !confirm_rebind) {
            return fail("rebind_confirmation_required", "该观测已绑定线索，重新绑定或解绑需要显式确认。");
        }
        // 步骤 6：已确认对比引用拦截（事务内再次校验）。
        if (old_thread_id.has_value() && referenced_by_confirmed_comparison(tx, observation_id)) {
            return fail(
                "observation_referenced_by_confirmed_comparison",
                "该观测已被人工确认的历史对比引用，须先撤销相关对比结论。");
        }

        const auto new_updated_at = update_observation_binding(tx, observation_id, defect_thread_id);
        // 步骤 8：新旧两条线索都按当前有效绑定重算首见/末见。
        if (old_thread_id.has_value()) {
            recompute_thread_span(tx, *old_thread_id);
        }
        if (defect_thread_id.has_value()) {
            recompute_thread_span(tx, *defect_thread_id);
        }

        ThreadBindingOutcome outcome;
        outcome.body["bound"] = defect_thread_id.has_value();
        outcome.body["defect_thread_id"] =
            defect_thread_id.has_value() ? Json::Value(*defect_thread_id) : Json::Value(Json::nullValue);
        if (defect_thread_id.has_value()) {
            outcome.body["defect_thread"] = thread_to_json(tx, *defect_thread_id);
        }
        outcome.body["observation_id"] = observation_id;
        outcome.body["observation_updated_at"] = new_updated_at;

        tx.reset();
        if (!latch->wait()) {
            return fail("database_commit_failed", "数据库提交失败。");
        }
        outcome.success = true;
        return outcome;
    } catch (const drogon::orm::DrogonDbException& exception) {
        return fail("db_write_failed", exception.base().what());
    } catch (const std::exception& exception) {
        return fail("db_write_failed", exception.what());
    }
}

}  // namespace bridge_report::db
