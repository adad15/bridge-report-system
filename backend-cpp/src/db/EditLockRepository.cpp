#include "bridge_report/db/EditLockRepository.hpp"

#include <memory>
#include <utility>

#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/resolution/ResolutionReopenSnapshot.hpp"

namespace bridge_report::db {
namespace {

EditLockInfo row_to_lock(const drogon::orm::Row& row) {
    EditLockInfo lock;
    lock.import_record_id = row["import_record_id"].as<std::string>();
    lock.user_id = row["user_id"].as<std::string>();
    lock.session_id = row["session_id"].as<std::string>();
    lock.username = row["username"].as<std::string>();
    lock.display_name = row["display_name"].as<std::string>();
    lock.acquired_at = row["acquired_at"].as<std::string>();
    lock.expires_at = row["expires_at"].as<std::string>();
    return lock;
}

const char* active_lock_select_sql() {
    return
        "select l.import_record_id::text as import_record_id, l.user_id::text as user_id, "
        "l.user_session_id::text as session_id, u.username, u.display_name, "
        "l.acquired_at::text as acquired_at, l.expires_at::text as expires_at "
        "from import_record_edit_locks l join users u on u.id = l.user_id ";
}

}  // namespace

EditLockRepository::EditLockRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

AcquireEditLockOutcome EditLockRepository::acquire(
    const std::string& import_record_id,
    const AuthUser& user
) {
    AcquireEditLockOutcome outcome;
    const auto token = auth::generate_session_token();
    const auto token_hash = auth::sha256_hex(token);
    const auto result = db_client_->execSqlSync(
        "insert into import_record_edit_locks "
        "(import_record_id, user_id, user_session_id, lock_token_hash, acquired_at, last_heartbeat_at, expires_at) "
        "select $1::uuid, $2::uuid, $3::uuid, $4, now(), now(), now() + interval '2 minutes' "
        "where exists (select 1 from import_records where id = $1::uuid) "
        "on conflict (import_record_id) do update set "
        "user_id = excluded.user_id, user_session_id = excluded.user_session_id, "
        "lock_token_hash = excluded.lock_token_hash, acquired_at = now(), "
        "last_heartbeat_at = now(), expires_at = now() + interval '2 minutes' "
        "where import_record_edit_locks.expires_at <= now() "
        "returning import_record_id::text as import_record_id",
        import_record_id,
        user.id,
        user.session_id,
        token_hash
    );
    if (!result.empty()) {
        outcome.acquired = true;
        outcome.lock_token = token;
        outcome.lock = get_active(import_record_id);
        return outcome;
    }

    outcome.lock = get_active(import_record_id);
    if (!outcome.lock.has_value()) {
        const auto found = db_client_->execSqlSync(
            "select exists(select 1 from import_records where id = $1::uuid) as found",
            import_record_id
        );
        outcome.import_record_found = !found.empty() && found[0]["found"].as<bool>();
    }
    return outcome;
}

AcquireEditLockOutcome EditLockRepository::acquire_and_reopen(
    const std::string& import_record_id,
    const AuthUser& user,
    const std::string& scope
) {
    AcquireEditLockOutcome outcome;
    const auto token = auth::generate_session_token();
    const auto token_hash = auth::sha256_hex(token);

    // 关系态快照必须与重开同事务（§8.8）。分两步时，中间崩一下就留下一个
    // 重开态但没有快照的记录，"放弃修改"再也回不到确认时的绑定。
    std::shared_ptr<drogon::orm::Transaction> tx;
    auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto result = tx->execSqlSync(
            "with eligible as ("
            "  select id from import_records where id = $1::uuid and import_status = '已确认' for update"
            "), locked as ("
            "  insert into import_record_edit_locks "
            "  (import_record_id, user_id, user_session_id, lock_token_hash, acquired_at, last_heartbeat_at, expires_at) "
            "  select id, $2::uuid, $3::uuid, $4, now(), now(), now() + interval '2 minutes' from eligible "
            "  on conflict (import_record_id) do update set "
            "    user_id = excluded.user_id, user_session_id = excluded.user_session_id, "
            "    lock_token_hash = excluded.lock_token_hash, acquired_at = now(), "
            "    last_heartbeat_at = now(), expires_at = now() + interval '2 minutes' "
            "  where import_record_edit_locks.expires_at <= now() "
            "  returning import_record_id"
            "), reopened as ("
            "  update import_records ir set import_status = '待校对', reopened_at = now(), "
            "    reopened_by_username = $5, reopen_scope = $6, "
            "    reopen_backup_parsed_result_json = parsed_result_json, updated_at = now() "
            "  from locked where ir.id = locked.import_record_id returning ir.id"
            ") select id::text as id from reopened",
            import_record_id,
            user.id,
            user.session_id,
            token_hash,
            user.username,
            scope
        );
        if (!result.empty()) {
            const auto snapshot = resolution::capture_reopen_snapshot(tx, import_record_id, user.id);
            if (!snapshot.success) {
                tx->rollback();
                tx.reset();
                outcome.lock = get_active(import_record_id);
                return outcome;
            }
            tx.reset();
            if (!latch->wait()) {
                outcome.lock = get_active(import_record_id);
                return outcome;
            }
            outcome.acquired = true;
            outcome.lock_token = token;
            outcome.lock = get_active(import_record_id);
            return outcome;
        }

        // 没拿到锁：事务里一个字也没改，直接回滚，下面的诊断查询走普通连接。
        tx->rollback();
        tx.reset();
        outcome.lock = get_active(import_record_id);
        if (!outcome.lock.has_value()) {
            const auto record = db_client_->execSqlSync(
                "select import_status from import_records where id = $1::uuid",
                import_record_id
            );
            outcome.import_record_found = !record.empty();
            outcome.import_record_state_changed = !record.empty()
                && record[0]["import_status"].as<std::string>() != "已确认";
        }
        return outcome;
    } catch (const std::exception&) {
        if (tx) {
            tx->rollback();
            tx.reset();
        }
        throw;
    }
}

std::optional<EditLockInfo> EditLockRepository::get_active(const std::string& import_record_id) {
    const auto result = db_client_->execSqlSync(
        std::string(active_lock_select_sql())
            + "where l.import_record_id = $1::uuid and l.expires_at > now()",
        import_record_id
    );
    if (result.empty()) {
        return std::nullopt;
    }
    return row_to_lock(result[0]);
}

EditLockCheckResult EditLockRepository::check(
    const std::string& import_record_id,
    const AuthUser& user,
    const std::string& lock_token
) {
    EditLockCheckResult outcome;
    if (lock_token.empty()) {
        outcome.status = EditLockCheckStatus::required;
        outcome.lock = get_active(import_record_id);
        return outcome;
    }

    const auto token_hash = auth::sha256_hex(lock_token);
    const auto exact = db_client_->execSqlSync(
        "select exists(select 1 from import_record_edit_locks "
        "where import_record_id = $1::uuid and user_id = $2::uuid and user_session_id = $3::uuid "
        "and lock_token_hash = $4 and expires_at > now()) as active",
        import_record_id,
        user.id,
        user.session_id,
        token_hash
    );
    if (!exact.empty() && exact[0]["active"].as<bool>()) {
        outcome.status = EditLockCheckStatus::active;
        outcome.lock = get_active(import_record_id);
        return outcome;
    }

    const auto current = db_client_->execSqlSync(
        "select lock_token_hash, expires_at <= now() as expired "
        "from import_record_edit_locks where import_record_id = $1::uuid",
        import_record_id
    );
    if (!current.empty()) {
        const bool same_token = current[0]["lock_token_hash"].as<std::string>() == token_hash;
        outcome.status = same_token && current[0]["expired"].as<bool>()
            ? EditLockCheckStatus::expired
            : EditLockCheckStatus::invalid;
        outcome.lock = get_active(import_record_id);
        return outcome;
    }

    const auto forced = db_client_->execSqlSync(
        "select exists(select 1 from import_record_edit_lock_events "
        "where import_record_id = $1::uuid and action = 'force_released' "
        "and previous_lock_token_hash = $2) as forced",
        import_record_id,
        token_hash
    );
    outcome.status = !forced.empty() && forced[0]["forced"].as<bool>()
        ? EditLockCheckStatus::force_released
        : EditLockCheckStatus::required;
    return outcome;
}

EditLockCheckResult EditLockRepository::heartbeat(
    const std::string& import_record_id,
    const AuthUser& user,
    const std::string& lock_token
) {
    if (lock_token.empty()) {
        return check(import_record_id, user, lock_token);
    }
    const auto result = db_client_->execSqlSync(
        "update import_record_edit_locks set last_heartbeat_at = now(), expires_at = now() + interval '2 minutes' "
        "where import_record_id = $1::uuid and user_id = $2::uuid and user_session_id = $3::uuid "
        "and lock_token_hash = $4 and expires_at > now() returning import_record_id",
        import_record_id,
        user.id,
        user.session_id,
        auth::sha256_hex(lock_token)
    );
    if (result.empty()) {
        return check(import_record_id, user, lock_token);
    }
    EditLockCheckResult outcome;
    outcome.status = EditLockCheckStatus::active;
    outcome.lock = get_active(import_record_id);
    return outcome;
}

bool EditLockRepository::release(
    const std::string& import_record_id,
    const AuthUser& user,
    const std::string& lock_token
) {
    if (lock_token.empty()) {
        return false;
    }
    const auto result = db_client_->execSqlSync(
        "delete from import_record_edit_locks "
        "where import_record_id = $1::uuid and user_id = $2::uuid and user_session_id = $3::uuid "
        "and lock_token_hash = $4 returning import_record_id",
        import_record_id,
        user.id,
        user.session_id,
        auth::sha256_hex(lock_token)
    );
    return !result.empty();
}

bool EditLockRepository::force_release(
    const std::string& import_record_id,
    const AuthUser& actor,
    const std::string& reason
) {
    const auto result = db_client_->execSqlSync(
        "with removed as ("
        "  delete from import_record_edit_locks where import_record_id = $1::uuid "
        "  returning user_id, lock_token_hash"
        ") "
        "insert into import_record_edit_lock_events "
        "(import_record_id, action, previous_owner_user_id, previous_lock_token_hash, actor_user_id, reason) "
        "select $1::uuid, 'force_released', user_id, lock_token_hash, $2::uuid, $3 from removed "
        "returning id",
        import_record_id,
        actor.id,
        reason
    );
    return !result.empty();
}

}  // namespace bridge_report::db
