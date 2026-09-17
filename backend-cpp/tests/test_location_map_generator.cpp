#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/report/LocationMapGenerator.hpp"

namespace {

namespace fs = std::filesystem;

drogon::orm::DbClientPtr shared_test_client() {
    static drogon::orm::DbClientPtr client = [] {
        const bridge_report::config::PostgresConfig config{};
        return bridge_report::db::create_db_client(config, 1);
    }();
    return client;
}

using bridge_report::report::LocationMapStatus;
using bridge_report::report::refresh_location_map;
using bridge_report::report::StaticMapFetcher;

/// 一张 1×1 的 PNG，够让文件头识别认出来。
std::string tiny_png() {
    const unsigned char bytes[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xcf, 0xc0, 0x50,
        0x0f, 0x00, 0x04, 0x85, 0x01, 0x80, 0x84, 0xa9, 0x8c, 0x21, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    return std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

// 生成报告前刷新地理位置图的判定。
//
// 取图那一步用桩顶替：drogon 的同步 HTTP 客户端离开运行中的后端进程会一直挂着，
// 真发请求会把整个测试进程卡死。真实取图由端到端验收覆盖。
class LocationMapGeneratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();
        bridge_id_ = client_->execSqlSync(
            "insert into bridges(bridge_name, longitude, latitude) values('地理位置图测试桥', 121.1963, 41.1153) "
            "returning id::text as id")[0]["id"].as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years(bridge_id, inspection_year) values($1::uuid, 2026) returning id::text as id",
            bridge_id_)[0]["id"].as<std::string>();
        archive_root_ = fs::temp_directory_path() / ("location-map-test-" + bridge_id_);
        fs::create_directories(archive_root_);
        config_.archive_root = archive_root_;
        config_.map.web_service_key = "test-key";
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        client_->execSqlSync("delete from bridge_media where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from archived_files where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", year_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        std::error_code ignored;
        fs::remove_all(archive_root_, ignored);
    }

    void insert_location_map(const std::string& source) {
        const auto file_id = client_->execSqlSync(
            "insert into archived_files(bridge_id,original_file_name,current_file_name,storage_relative_path,"
            "file_type,file_purpose,file_extension,file_size_bytes) values($1::uuid,'map.png','map.png',"
            "'bridges/test/media/map.png','图片','桥梁图件·地理位置图','.png',1) returning id::text as id",
            bridge_id_)[0]["id"].as<std::string>();
        client_->execSqlSync(
            "insert into bridge_media(bridge_id,slot,archived_file_id,source) values($1::uuid,'LOCATION_MAP',$2::uuid,$3)",
            bridge_id_, file_id, source);
    }

    std::string stored_source() {
        const auto rows = client_->execSqlSync(
            "select source from bridge_media where bridge_id=$1::uuid and slot='LOCATION_MAP'", bridge_id_);
        return rows.empty() ? std::string() : rows[0]["source"].as<std::string>();
    }

    /// 被调了就记一笔，返回一张图。
    StaticMapFetcher succeeding_fetcher() {
        return [this](const Json::Value& ask) {
            ++fetch_calls_;
            last_request_ = ask;
            return tiny_png();
        };
    }

    StaticMapFetcher failing_fetcher() {
        return [this](const Json::Value&) -> std::string {
            ++fetch_calls_;
            throw std::runtime_error("地图服务返回：INVALID_USER_KEY。");
        };
    }

    drogon::orm::DbClientPtr client_;
    bridge_report::config::AppConfig config_;
    fs::path archive_root_;
    std::string bridge_id_;
    std::string year_id_;
    int fetch_calls_{0};
    Json::Value last_request_;
};

TEST_F(LocationMapGeneratorTest, GeneratesFromTheStoredCoordinates) {
    const auto outcome = refresh_location_map(client_, config_, year_id_, succeeding_fetcher());

    EXPECT_EQ(outcome.status, LocationMapStatus::Generated);
    EXPECT_EQ(stored_source(), "按坐标生成");
    // 坐标原样按 WGS-84 交出去，换算在取图服务那边做。
    EXPECT_DOUBLE_EQ(last_request_["longitude"].asDouble(), 121.1963);
    EXPECT_DOUBLE_EQ(last_request_["latitude"].asDouble(), 41.1153);
    EXPECT_EQ(last_request_["key"].asString(), "test-key");
}

// 每次生成报告都重新取：坐标改过之后，图也就跟着对了。
TEST_F(LocationMapGeneratorTest, RegeneratesAPreviouslyGeneratedMap) {
    insert_location_map("按坐标生成");

    EXPECT_EQ(refresh_location_map(client_, config_, year_id_, succeeding_fetcher()).status,
              LocationMapStatus::Generated);
    EXPECT_EQ(fetch_calls_, 1);
}

// 管理员自己传的地理位置图通常是标注过的截图，比自动取的合用，绝不能被覆盖。
TEST_F(LocationMapGeneratorTest, KeepsAManuallyUploadedMapWithoutFetching) {
    insert_location_map("人工上传");

    EXPECT_EQ(refresh_location_map(client_, config_, year_id_, succeeding_fetcher()).status,
              LocationMapStatus::KeptManual);
    EXPECT_EQ(fetch_calls_, 0);
    EXPECT_EQ(stored_source(), "人工上传");
}

TEST_F(LocationMapGeneratorTest, DoesNothingWithoutCoordinates) {
    client_->execSqlSync("update bridges set longitude=null, latitude=null where id=$1::uuid", bridge_id_);

    EXPECT_EQ(refresh_location_map(client_, config_, year_id_, succeeding_fetcher()).status,
              LocationMapStatus::NoCoordinates);
    EXPECT_EQ(fetch_calls_, 0);
}

TEST_F(LocationMapGeneratorTest, DoesNothingWithoutAWebServiceKey) {
    config_.map.web_service_key.clear();

    EXPECT_EQ(refresh_location_map(client_, config_, year_id_, succeeding_fetcher()).status,
              LocationMapStatus::NoKey);
    EXPECT_EQ(fetch_calls_, 0);
}

// 取图失败绝不能让报告失败：有上一次生成的就沿用。
TEST_F(LocationMapGeneratorTest, KeepsThePreviousGeneratedMapWhenFetchingFails) {
    insert_location_map("按坐标生成");

    const auto outcome = refresh_location_map(client_, config_, year_id_, failing_fetcher());

    EXPECT_EQ(outcome.status, LocationMapStatus::FailedKeptExisting);
    EXPECT_NE(outcome.detail.find("INVALID_USER_KEY"), std::string::npos);
    EXPECT_EQ(stored_source(), "按坐标生成");
}

TEST_F(LocationMapGeneratorTest, ReportsNoMapWhenFetchingFailsAndNoneExists) {
    EXPECT_EQ(refresh_location_map(client_, config_, year_id_, failing_fetcher()).status,
              LocationMapStatus::FailedNone);
    EXPECT_EQ(stored_source(), "");
}

TEST_F(LocationMapGeneratorTest, RejectsAResponseThatIsNotAnImage) {
    const StaticMapFetcher not_an_image = [](const Json::Value&) { return std::string("{\"info\":\"x\"}"); };

    EXPECT_EQ(refresh_location_map(client_, config_, year_id_, not_an_image).status,
              LocationMapStatus::FailedNone);
}

}  // namespace
