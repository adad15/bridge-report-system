#include "bridge_report/db/AuthRepository.hpp"

#include <utility>

#include "bridge_report/auth/PasswordHash.hpp"

namespace bridge_report::db {
namespace {

AuthUser row_to_auth_user(const drogon::orm::Row& row, std::string session_id = std::string()) {
    AuthUser user;
    user.id = row["id"].as<std::string>();
    user.session_id = std::move(session_id);
    user.username = row["username"].as<std::string>();
    user.display_name = row["display_name"].as<std::string>();
    user.role = row["role"].as<std::string>();
    return user;
}

}  // namespace

AuthRepository::AuthRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

std::optional<std::pair<AuthUser, std::string>> AuthRepository::login(
    const std::string& username,
    const std::string& password
) {
    const auto result = db_client_->execSqlSync(
        "select id::text as id, username, display_name, password_hash, role "
        "from users where username = $1 and is_active",
        username
    );
    if (result.empty()) {
        return std::nullopt;
    }
    const auto& row = result[0];
    if (!auth::verify_password(password, row["password_hash"].as<std::string>())) {
        return std::nullopt;
    }

    // 顺手清理过期会话，避免表无限增长（本地单机量级，直接删除即可）。
    db_client_->execSqlSync("delete from user_sessions where expires_at < now()");

    const auto token = auth::generate_session_token();
    const auto session = db_client_->execSqlSync(
        "insert into user_sessions (user_id, token_hash, expires_at) "
        "values ($1::uuid, $2, now() + interval '12 hours') returning id::text as session_id",
        row["id"].as<std::string>(),
        auth::sha256_hex(token)
    );

    auto user = row_to_auth_user(row, session[0]["session_id"].as<std::string>());
    return std::make_pair(std::move(user), token);
}

std::optional<AuthUser> AuthRepository::find_user_by_token(const std::string& token) {
    if (token.empty()) {
        return std::nullopt;
    }
    const auto result = db_client_->execSqlSync(
        "select u.id::text as id, s.id::text as session_id, u.username, u.display_name, u.role "
        "from user_sessions s join users u on u.id = s.user_id "
        "where s.token_hash = $1 and s.expires_at > now() and u.is_active",
        auth::sha256_hex(token)
    );
    if (result.empty()) {
        return std::nullopt;
    }
    return row_to_auth_user(result[0], result[0]["session_id"].as<std::string>());
}

void AuthRepository::delete_session_by_token(const std::string& token) {
    if (token.empty()) {
        return;
    }
    db_client_->execSqlSync(
        "delete from user_sessions where token_hash = $1",
        auth::sha256_hex(token)
    );
}

int AuthRepository::seed_default_users() {
    const auto existing = db_client_->execSqlSync("select count(*)::int as n from users");
    if (!existing.empty() && existing[0]["n"].as<int>() > 0) {
        return 0;
    }
    db_client_->execSqlSync(
        "insert into users (username, display_name, password_hash, role) values ($1, $2, $3, 'admin')",
        std::string("admin"),
        std::string("管理员"),
        auth::hash_password("admin123")
    );
    db_client_->execSqlSync(
        "insert into users (username, display_name, password_hash, role) values ($1, $2, $3, 'normal')",
        std::string("user"),
        std::string("普通用户"),
        auth::hash_password("user123")
    );
    return 2;
}

}  // namespace bridge_report::db
