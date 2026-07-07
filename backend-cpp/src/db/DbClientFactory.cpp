#include "bridge_report/db/DbClientFactory.hpp"

#include <sstream>

namespace bridge_report::db {

std::string build_pg_connection_string(const config::PostgresConfig& config) {
    std::ostringstream stream;
    stream << "host=" << config.host
           << " port=" << config.port
           << " dbname=" << config.database
           << " user=" << config.user
           << " password=" << config.password;
    return stream.str();
}

drogon::orm::DbClientPtr create_db_client(const config::PostgresConfig& config, size_t connection_count) {
    // newPgClient 不会立即建立连接；连接失败会在首次查询时才暴露出来。
    return drogon::orm::DbClient::newPgClient(build_pg_connection_string(config), connection_count);
}

}  // 命名空间 bridge_report::db
