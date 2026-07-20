#include <cstdlib>
#include <stdexcept>
#include <string>

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>
#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"

TEST(DbClientFactoryTest, build_pg_connection_string_formats_all_fields) {
    const bridge_report::config::PostgresConfig config{};

    const auto connection_string = bridge_report::db::build_pg_connection_string(config);

    EXPECT_EQ(
        connection_string,
        "host='127.0.0.1' port='5432' dbname='bridge_report_system' "
        "user='bridge_report' password='bridge_report_dev'"
    );
}

TEST(DbClientFactoryTest, build_pg_connection_string_escapes_special_characters) {
    // libpq 关键字/值连接串中，值内的反斜杠和单引号必须转义：\ -> \\，' -> \'。
    bridge_report::config::PostgresConfig config{};
    config.password = R"(p'a s\s)";

    const auto connection_string = bridge_report::db::build_pg_connection_string(config);

    EXPECT_EQ(
        connection_string,
        "host='127.0.0.1' port='5432' dbname='bridge_report_system' "
        "user='bridge_report' password='p\\'a s\\\\s'"
    );
}

TEST(DbClientFactoryTest, create_db_client_connects_and_selects_one) {
    const char* env_value = std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL");
    if (env_value == nullptr) {
        GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
    }

    // 测试 URL 与隔离 schema 由开发脚本成对提供；create_db_client 会拒绝 public
    // 或非 bridge_report_test* schema，避免集成测试写入开发业务表。
    const bridge_report::config::PostgresConfig config{};
    auto client = bridge_report::db::create_db_client(config, 1);

    const auto result = client->execSqlSync("select 1");

    EXPECT_EQ(result.size(), 1u);
}

TEST(DbClientFactoryTest, create_db_client_rejects_test_url_without_isolated_schema) {
    const char* original_url = std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL");
    const char* original_schema = std::getenv("BRIDGE_REPORT_TEST_SCHEMA");
    const std::string saved_url = original_url == nullptr ? "" : original_url;
    const std::string saved_schema = original_schema == nullptr ? "" : original_schema;

#ifdef _WIN32
    _putenv_s("BRIDGE_REPORT_TEST_DATABASE_URL", "postgresql://example.invalid/bridge_report_system");
    _putenv_s("BRIDGE_REPORT_TEST_SCHEMA", "public");
#else
    setenv("BRIDGE_REPORT_TEST_DATABASE_URL", "postgresql://example.invalid/bridge_report_system", 1);
    setenv("BRIDGE_REPORT_TEST_SCHEMA", "public", 1);
#endif

    const bridge_report::config::PostgresConfig config{};
    EXPECT_THROW(bridge_report::db::create_db_client(config, 1), std::invalid_argument);

#ifdef _WIN32
    _putenv_s("BRIDGE_REPORT_TEST_DATABASE_URL", saved_url.c_str());
    _putenv_s("BRIDGE_REPORT_TEST_SCHEMA", saved_schema.c_str());
#else
    if (saved_url.empty()) unsetenv("BRIDGE_REPORT_TEST_DATABASE_URL");
    else setenv("BRIDGE_REPORT_TEST_DATABASE_URL", saved_url.c_str(), 1);
    if (saved_schema.empty()) unsetenv("BRIDGE_REPORT_TEST_SCHEMA");
    else setenv("BRIDGE_REPORT_TEST_SCHEMA", saved_schema.c_str(), 1);
#endif
}
