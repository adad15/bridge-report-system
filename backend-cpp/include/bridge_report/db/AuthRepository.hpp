#pragma once

#include <optional>
#include <string>
#include <utility>

#include <drogon/orm/DbClient.h>

namespace bridge_report::db {

/// 已通过会话校验的登录用户视图（不含任何口令信息）。
struct AuthUser {
    std::string id;
    std::string session_id;
    std::string username;
    std::string display_name;
    std::string role;  // 'admin' | 'normal'

    bool is_admin() const { return role == "admin"; }
};

/**
 * @brief 轻量账号体系的唯一数据访问入口：users / user_sessions 两张表。
 *
 * 第一版没有注册与用户管理界面：默认账号由 seed_default_users 在启动时播种，
 * 改密码走 SQL（见 docs）。会话表只存令牌的 SHA-256，比对前先做同样的单向变换。
 */
class AuthRepository {
public:
    explicit AuthRepository(drogon::orm::DbClientPtr db_client);

    /// 校验用户名/口令；成功则创建 12 小时会话并返回（用户, 明文令牌）。
    /// 用户不存在、被停用或口令不符都返回 nullopt，不区分原因（避免枚举用户名）。
    std::optional<std::pair<AuthUser, std::string>> login(
        const std::string& username,
        const std::string& password
    );

    /// 按明文令牌查有效会话对应的用户（过期/停用/不存在 -> nullopt）。
    std::optional<AuthUser> find_user_by_token(const std::string& token);

    /// 登出：删除该令牌的会话；令牌不存在也视为成功（幂等）。
    void delete_session_by_token(const std::string& token);

    /// users 表为空时播种默认账号 admin/admin123（管理员）与 user/user123（普通）。
    /// @return 本次插入的账号数（0 表示已有账号，未做任何事）。
    int seed_default_users();

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
