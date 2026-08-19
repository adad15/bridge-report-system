#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/db/AuthRepository.hpp"

namespace bridge_report::db {

// 一次写请求携带的编辑锁凭证。写仓储在事务内用它复查锁是否仍然有效：
// 路由层那道检查发生在事务外，从检查到真正写入之间锁可能过期或被管理员强制收回。
struct EditLockCredentials {
    std::string user_id;
    std::string session_id;
    std::string lock_token;
};

struct EditLockInfo {
    std::string import_record_id;
    std::string user_id;
    std::string session_id;
    std::string username;
    std::string display_name;
    std::string acquired_at;
    std::string expires_at;
};

struct AcquireEditLockOutcome {
    bool acquired{false};
    bool import_record_found{true};
    bool import_record_state_changed{false};
    std::string lock_token;
    std::optional<EditLockInfo> lock;
};

enum class EditLockCheckStatus {
    active,
    required,
    invalid,
    expired,
    force_released,
};

struct EditLockCheckResult {
    EditLockCheckStatus status{EditLockCheckStatus::required};
    std::optional<EditLockInfo> lock;

    bool active() const { return status == EditLockCheckStatus::active; }
};

class EditLockRepository {
public:
    explicit EditLockRepository(drogon::orm::DbClientPtr db_client);

    AcquireEditLockOutcome acquire(const std::string& import_record_id, const AuthUser& user);
    AcquireEditLockOutcome acquire_and_reopen(
        const std::string& import_record_id,
        const AuthUser& user,
        const std::string& scope
    );
    std::optional<EditLockInfo> get_active(const std::string& import_record_id);
    EditLockCheckResult check(
        const std::string& import_record_id,
        const AuthUser& user,
        const std::string& lock_token
    );
    EditLockCheckResult heartbeat(
        const std::string& import_record_id,
        const AuthUser& user,
        const std::string& lock_token
    );
    bool release(
        const std::string& import_record_id,
        const AuthUser& user,
        const std::string& lock_token
    );
    bool force_release(
        const std::string& import_record_id,
        const AuthUser& actor,
        const std::string& reason
    );

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
