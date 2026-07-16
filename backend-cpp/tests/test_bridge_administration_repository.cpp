#include <cstdlib>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/BridgeAdministrationRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"

TEST(BridgeAdministrationRepositoryTest, CreatesNormalizedBridgeAndRejectsDuplicateIdentity) {
    if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) GTEST_SKIP();
    const auto client = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{}, 1);
    bridge_report::db::BridgeAdministrationRepository repository(client);
    bridge_report::db::CreateBridgeRequest request;
    request.bridge_name = "  仓储新增测试桥  ";
    request.route_number = " s213 ";
    request.station_mark = " K1+000 ";

    const auto created = repository.create_bridge(request);
    ASSERT_EQ(created.status, bridge_report::db::CreateBridgeStatus::Created);
    ASSERT_TRUE(created.bridge.has_value());
    EXPECT_EQ(created.bridge->bridge_name, "仓储新增测试桥");
    EXPECT_EQ(created.bridge->route_number, "s213");

    request.bridge_name = "仓储新增测试桥";
    request.route_number = "S213";
    request.station_mark = "k1+000";
    const auto duplicate = repository.create_bridge(request);
    EXPECT_EQ(duplicate.status, bridge_report::db::CreateBridgeStatus::Duplicate);
    ASSERT_TRUE(duplicate.bridge.has_value());
    EXPECT_EQ(duplicate.bridge->id, created.bridge->id);

    client->execSqlSync("delete from bridges where id=$1::uuid", created.bridge->id);
    client->closeAll();
}
