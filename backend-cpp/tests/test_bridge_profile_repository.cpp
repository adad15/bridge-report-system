#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/BridgeProfileRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"

namespace {

/// 整个进程共用一个连接：max_connections 是 100，而每个用例各开一个 DbClient 会在
/// 全量跑时把连接池耗尽（表现为满屏 "connection pointer is NULL"）。
drogon::orm::DbClientPtr shared_test_client() {
    static drogon::orm::DbClientPtr client = [] {
        const bridge_report::config::PostgresConfig config{};
        return bridge_report::db::create_db_client(config, 1);
    }();
    return client;
}

using bridge_report::report::BridgeProfileInput;
using bridge_report::report::BridgeProfileWriteStatus;

// 桥梁档案读写的数据库集成夹具。
//
// 这一份数据同时喂给报告 §1.1 的叙述和附录2 卡片，所以关心的是三件事：整体覆盖
// 能表达"把录错的项清空"、量值的合法区间由数据库约束兜住、桥梁不存在时给出业务
// 结论而不是把外键异常抛到上层。
class BridgeProfileRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();
        const auto result = client_->execSqlSync(
            "insert into bridges (bridge_name) values ('桥梁档案测试桥') returning id");
        bridge_id_ = result[0]["id"].as<std::string>();
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
    }

    bridge_report::db::BridgeProfileRepository repository() {
        return bridge_report::db::BridgeProfileRepository(client_);
    }

    /// 档案录满的一座桥。
    static BridgeProfileInput full_input() {
        BridgeProfileInput input;
        input.business_code = "L0123";
        input.route_number = "S320";
        input.route_name = "大养线";
        input.administrative_region = "太和区";
        input.station_mark = "K12+345";
        input.longitude = 121.1352;
        input.latitude = 41.0967;
        input.bridge_type = "简支板桥";
        input.bridge_scale = "中桥";
        input.span_combination = "5×13m";
        input.bridge_length_m = 68.5;
        input.bridge_width_m = 12.0;
        input.built_year = 1998;
        input.skew_angle_deg = 90.0;
        input.carriageway_width_m = 11.0;
        input.sidewalk_width_m = 0.5;
        input.deck_pavement = "水泥混凝土";
        input.expansion_joint_type = "橡胶伸缩缝";
        input.expansion_joint_piers = "1、4";
        input.bearing_type = "板式橡胶支座";
        input.superstructure_form = "预应力混凝土简支空心板";
        input.girders_per_span = 9;
        input.girder_height_m = 0.7;
        input.abutment_form = "桩柱式桥台";
        input.pier_form = "柱式墩";
        input.foundation_form = "钻孔灌注桩基础";
        input.design_load = "公路-Ⅰ级";
        input.design_org = "某某设计院";
        input.construction_org = "某某工程局";
        input.maintenance_org = "太和公路段";
        input.supervision_org = "某某公路管理处";
        return input;
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
};

TEST_F(BridgeProfileRepositoryTest, SavesAndReadsBackEveryField) {
    ASSERT_EQ(repository().save(bridge_id_, full_input()), BridgeProfileWriteStatus::Ok);

    const auto profile = repository().find(bridge_id_);
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile->bridge_name, "桥梁档案测试桥");
    EXPECT_EQ(profile->station_mark, "K12+345");
    EXPECT_EQ(profile->span_combination, "5×13m");
    EXPECT_EQ(profile->built_year, 1998);
    EXPECT_EQ(profile->girders_per_span, 9);
    EXPECT_EQ(profile->expansion_joint_piers, "1、4");
    EXPECT_EQ(profile->design_load, "公路-Ⅰ级");
    EXPECT_EQ(profile->supervision_org, "某某公路管理处");
    ASSERT_TRUE(profile->longitude.has_value());
    EXPECT_DOUBLE_EQ(*profile->longitude, 121.1352);
    ASSERT_TRUE(profile->latitude.has_value());
    EXPECT_DOUBLE_EQ(*profile->latitude, 41.0967);
    ASSERT_TRUE(profile->girder_height_m.has_value());
    EXPECT_DOUBLE_EQ(*profile->girder_height_m, 0.7);
    ASSERT_TRUE(profile->sidewalk_width_m.has_value());
    EXPECT_DOUBLE_EQ(*profile->sidewalk_width_m, 0.5);
}

TEST_F(BridgeProfileRepositoryTest, EmptyProfileReadsBackAsAllAbsent) {
    const auto profile = repository().find(bridge_id_);
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile->bridge_id, bridge_id_);
    EXPECT_FALSE(profile->station_mark.has_value());
    EXPECT_FALSE(profile->built_year.has_value());
    EXPECT_FALSE(profile->girder_height_m.has_value());
}

TEST_F(BridgeProfileRepositoryTest, WholeRecordOverwriteCanClearAField) {
    // 录错了要能改回空。增量合并做不到这件事，所以写是整体覆盖。
    ASSERT_EQ(repository().save(bridge_id_, full_input()), BridgeProfileWriteStatus::Ok);

    auto cleared = full_input();
    cleared.station_mark = std::nullopt;
    cleared.girder_height_m = std::nullopt;
    cleared.design_org = std::nullopt;
    ASSERT_EQ(repository().save(bridge_id_, cleared), BridgeProfileWriteStatus::Ok);

    const auto profile = repository().find(bridge_id_);
    ASSERT_TRUE(profile.has_value());
    EXPECT_FALSE(profile->station_mark.has_value());
    EXPECT_FALSE(profile->girder_height_m.has_value());
    EXPECT_FALSE(profile->design_org.has_value());
    // 没动的项不受影响。
    EXPECT_EQ(profile->route_name, "大养线");
}

TEST_F(BridgeProfileRepositoryTest, BlankStringsCountAsNotFilledIn) {
    // 编辑界面清空输入框常常留下一个空格，那不算"录了值"。
    auto input = full_input();
    input.station_mark = "   ";
    input.design_org = "";
    ASSERT_EQ(repository().save(bridge_id_, input), BridgeProfileWriteStatus::Ok);

    const auto profile = repository().find(bridge_id_);
    ASSERT_TRUE(profile.has_value());
    EXPECT_FALSE(profile->station_mark.has_value());
    EXPECT_FALSE(profile->design_org.has_value());
}

TEST_F(BridgeProfileRepositoryTest, OutOfRangeMeasuresAreRejectedAsABusinessResult) {
    auto input = full_input();
    input.girders_per_span = 0;
    EXPECT_EQ(repository().save(bridge_id_, input),
              BridgeProfileWriteStatus::MeasureOutOfRange);

    input = full_input();
    input.skew_angle_deg = 200.0;
    EXPECT_EQ(repository().save(bridge_id_, input),
              BridgeProfileWriteStatus::MeasureOutOfRange);

    // 经纬度填反是最常见的录入错误，反了之后纬度会超出 ±90。
    input = full_input();
    input.longitude = 41.0967;
    input.latitude = 121.1352;
    EXPECT_EQ(repository().save(bridge_id_, input),
              BridgeProfileWriteStatus::MeasureOutOfRange);

    // 被拒的写不留半份数据。
    const auto profile = repository().find(bridge_id_);
    ASSERT_TRUE(profile.has_value());
    EXPECT_FALSE(profile->girders_per_span.has_value());
}

TEST_F(BridgeProfileRepositoryTest, MissingBridgeIsABusinessResultNotAnException) {
    const std::string absent = "00000000-0000-0000-0000-000000000000";
    EXPECT_FALSE(repository().find(absent).has_value());
    EXPECT_EQ(repository().save(absent, full_input()),
              BridgeProfileWriteStatus::BridgeNotFound);
}

}  // namespace
