#pragma once

#include <optional>

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>

#include "bridge_report/db/AuthRepository.hpp"

namespace bridge_report::http {

/**
 * @brief 从 `Authorization: Bearer <token>` 解析当前登录用户。
 *
 * 未带头、格式不对、会话过期或用户被停用都返回 nullopt。
 * 内部查询数据库，DrogonDbException 由调用方处理器的 try/catch 兜底。
 * 供所有需要登录的写类端点在处理器开头调用。
 */
std::optional<db::AuthUser> authenticate_request(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request
);

/// POST /api/auth/login、POST /api/auth/logout、GET /api/auth/me。
void register_auth_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
