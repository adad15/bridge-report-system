#include "bridge_report/db/DbClientFactory.hpp"

#include <cstdlib>
#include <stdexcept>
#include <sstream>
#include <string_view>

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

bool is_safe_test_schema(const std::string_view schema) {
    constexpr std::string_view prefix = "bridge_report_test";
    if (!schema.starts_with(prefix)) {
        return false;
    }
    for (const char ch : schema) {
        const bool valid = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
        if (!valid) {
            return false;
        }
    }
    return true;
}

std::string nonempty_environment_value(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{value};
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
    const auto test_database_url = nonempty_environment_value("BRIDGE_REPORT_TEST_DATABASE_URL");
    const auto test_schema = nonempty_environment_value("BRIDGE_REPORT_TEST_SCHEMA");
    if (test_database_url.empty() != test_schema.empty()) {
        throw std::invalid_argument(
            "BRIDGE_REPORT_TEST_DATABASE_URL and BRIDGE_REPORT_TEST_SCHEMA must be set together");
    }
    if (!test_schema.empty() && !is_safe_test_schema(test_schema)) {
        throw std::invalid_argument(
            "refusing to run database tests outside a bridge_report_test* schema");
    }

    // newPgClient 不会立即建立连接；连接失败会在首次查询时才暴露出来。
    auto client = drogon::orm::DbClient::newPgClient(
        test_database_url.empty() ? build_pg_connection_string(config) : test_database_url,
        connection_count);
    // 数据库不可达时 drogon 会无限重连并挂起排队的查询；设置超时让
    // execSqlSync 抛出 TimeoutError（属于 DrogonDbException），调用方才能把
    // 数据库故障转成 503 而不是无限阻塞 IO 线程。
    client->setTimeout(10.0);
    if (!test_schema.empty()) {
        // schema 已通过严格的小写字母/数字/下划线校验，可安全用于标识符。
        // 集成测试均使用单连接客户端，设置一次 search_path 即覆盖该夹具的全部查询。
        client->execSqlSync("set search_path to " + test_schema);
    }
    return client;
}

}  // 命名空间 bridge_report::db
