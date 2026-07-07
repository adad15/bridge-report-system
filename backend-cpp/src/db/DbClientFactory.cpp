#include "bridge_report/db/DbClientFactory.hpp"

#include <sstream>

namespace bridge_report::db {

namespace {

// libpq 关键字/值连接串：值统一加单引号，值内的 \ 和 ' 需转义（\ -> \\，' -> \'）。
std::string quote_pg_value(const std::string& value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('\'');
    for (const char ch : value) {
        if (ch == '\\' || ch == '\'') {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('\'');
    return quoted;
}

}  // 匿名命名空间

std::string build_pg_connection_string(const config::PostgresConfig& config) {
    std::ostringstream stream;
    stream << "host=" << quote_pg_value(config.host)
           << " port=" << quote_pg_value(std::to_string(config.port))
           << " dbname=" << quote_pg_value(config.database)
           << " user=" << quote_pg_value(config.user)
           << " password=" << quote_pg_value(config.password);
    return stream.str();
}

drogon::orm::DbClientPtr create_db_client(const config::PostgresConfig& config, size_t connection_count) {
    // newPgClient 不会立即建立连接；连接失败会在首次查询时才暴露出来。
    return drogon::orm::DbClient::newPgClient(build_pg_connection_string(config), connection_count);
}

}  // 命名空间 bridge_report::db
