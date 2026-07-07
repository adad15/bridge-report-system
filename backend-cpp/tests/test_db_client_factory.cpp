#include <cstdlib>
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
        "host=127.0.0.1 port=5432 dbname=bridge_report_system user=bridge_report password=bridge_report_dev"
    );
}

TEST(DbClientFactoryTest, create_db_client_connects_and_selects_one) {
    const char* env_value = std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL");
    if (env_value == nullptr) {
        GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
    }

    // 前提：本地测试数据库须与模块 02 的默认连接参数一致，
    // 即默认 PostgresConfig 经 build_pg_connection_string 生成的连接串可以直接连上。
    const bridge_report::config::PostgresConfig config{};
    auto client = bridge_report::db::create_db_client(config, 1);

    const auto result = client->execSqlSync("select 1");

    EXPECT_EQ(result.size(), 1u);
}
