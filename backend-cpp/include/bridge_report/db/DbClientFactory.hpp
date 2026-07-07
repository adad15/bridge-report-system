#pragma once

#include <cstddef>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/config/AppConfig.hpp"

namespace bridge_report::db {

/**
 * @brief 根据 PostgresConfig 构造 libpq 风格的连接串。
 */
std::string build_pg_connection_string(const config::PostgresConfig& config);

/**
 * @brief 创建 PostgreSQL DbClient；连接在首次查询时才会真正建立。
 */
drogon::orm::DbClientPtr create_db_client(
    const config::PostgresConfig& config,
    size_t connection_count = 2
);

}  // 命名空间 bridge_report::db
