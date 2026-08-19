#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/deletion/TemporaryWordCleanupCoordinator.hpp"

namespace {

class TemporaryWordCleanupCoordinatorTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});
        bridge_id_ = client_->execSqlSync(
            "insert into bridges(bridge_name) values('临时Word清理测试桥') returning id")[0]["id"].as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years(bridge_id,inspection_year,status,is_current) "
            "values($1::uuid,2041,'待校对',true) returning id", bridge_id_)[0]["id"].as<std::string>();
        root_ = std::filesystem::temp_directory_path() / ("temporary-word-cleanup-" + bridge_id_);
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }

    void TearDown() override {
        if (!client_) return;
        // 必须先删来源文件行：import_source_files.import_record_id 是 on delete set null，
        // 先删导入记录的话这些行会变成孤儿永远留在库里，日后到期又被别的测试的
        // 全局计数认领，表现为毫不相干的测试偶发失败。
        client_->execSqlSync(
            "delete from import_source_files sf using import_records ir "
            "where sf.import_record_id = ir.id and ir.bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from import_records where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", year_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        client_->closeAll();
        std::filesystem::remove_all(root_);
    }

    std::string source(const std::string& import_status, const std::string& source_status,
                       const std::string& timing_sql, bool create_file) {
        const auto import_id = client_->execSqlSync(
            "insert into import_records(bridge_id,inspection_year_id,import_name,source_type,import_status) "
            "values($1::uuid,$2::uuid,'清理测试.docx','正式Word',$3) returning id",
            bridge_id_, year_id_, import_status)[0]["id"].as<std::string>();
        const auto row = client_->execSqlSync(
            "with source_id as(select gen_random_uuid() id) "
            "insert into import_source_files(id,import_record_id,original_file_name,storage_relative_path,"
            "file_extension,file_size_bytes,file_hash,status,expires_at,parsing_started_at,cleanup_reason,next_cleanup_at) "
            "select id,$1::uuid,'清理测试.docx',id::text||'.docx','.docx',4,$2,$3," + timing_sql +
            " from source_id returning id::text,storage_relative_path",
            import_id, std::string(64, 'b'), source_status);
        const auto path = row[0]["storage_relative_path"].as<std::string>();
        if (create_file) std::ofstream(root_ / path, std::ios::binary) << "docx";
        return row[0]["id"].as<std::string>();
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string year_id_;
    std::filesystem::path root_;
};

TEST_F(TemporaryWordCleanupCoordinatorTest, DeletesPendingAndExpiredButRetainsUnexpiredFailure) {
    const auto pending = source("待校对", "待清理", "null,null,'解析成功',now()", true);
    const auto expired = source("解析失败", "解析失败", "now()-interval '1 minute',null,null,null", true);
    const auto retained = source("解析失败", "解析失败", "now()+interval '24 hours',null,null,null", true);
    bridge_report::deletion::TemporaryWordCleanupPolicy policy;
    policy.batch_size = 10;
    bridge_report::deletion::TemporaryWordCleanupCoordinator coordinator(client_, root_, policy);

    const auto summary = coordinator.process_pending();

    // 认领是全局的，不能断言绝对值——库里任何一行别的可清理数据都会把它顶掉。
    // 真正的判据是下面那三行各自的终态。
    EXPECT_GE(summary.claimed, 2);
    EXPECT_EQ(summary.completed, summary.claimed) << "认领了就必须处理完";
    EXPECT_EQ(client_->execSqlSync("select status from import_source_files where id=$1::uuid", pending)[0]["status"].as<std::string>(), "已删除");
    EXPECT_EQ(client_->execSqlSync("select status from import_source_files where id=$1::uuid", expired)[0]["status"].as<std::string>(), "已过期");
    EXPECT_EQ(client_->execSqlSync("select status from import_source_files where id=$1::uuid", retained)[0]["status"].as<std::string>(), "解析失败");
}

TEST_F(TemporaryWordCleanupCoordinatorTest, RecoversStaleParsingAndTreatsMissingFileAsDeleted) {
    const auto stale = source("解析中", "解析中", "null,now()-interval '1 hour',null,null", true);
    const auto missing = source("待校对", "待清理", "null,null,'解析成功',now()", false);
    bridge_report::deletion::TemporaryWordCleanupPolicy policy;
    policy.parsing_timeout_seconds = 60;
    policy.failed_retention_hours = 24;
    bridge_report::deletion::TemporaryWordCleanupCoordinator coordinator(client_, root_, policy);

    const auto summary = coordinator.process_pending();

    EXPECT_GE(summary.recovered_parses, 1);  // 同样是全局计数，见上。
    EXPECT_EQ(client_->execSqlSync("select status from import_source_files where id=$1::uuid", stale)[0]["status"].as<std::string>(), "解析失败");
    EXPECT_EQ(client_->execSqlSync("select status from import_source_files where id=$1::uuid", missing)[0]["status"].as<std::string>(), "已删除");
}

TEST_F(TemporaryWordCleanupCoordinatorTest, RemovesOldOrphanButKeepsRecentUncommittedCandidate) {
    const auto old_orphan = root_ / "11111111-1111-1111-1111-111111111111.docx";
    const auto recent = root_ / "22222222-2222-2222-2222-222222222222.docx";
    std::ofstream(old_orphan, std::ios::binary) << "old";
    std::ofstream(recent, std::ios::binary) << "recent";
    std::filesystem::last_write_time(
        old_orphan, std::filesystem::file_time_type::clock::now() - std::chrono::hours(2));
    bridge_report::deletion::TemporaryWordCleanupPolicy policy;
    policy.parsing_timeout_seconds = 60;
    bridge_report::deletion::TemporaryWordCleanupCoordinator coordinator(client_, root_, policy);

    const auto summary = coordinator.process_pending();

    EXPECT_EQ(summary.orphaned_removed, 1);
    EXPECT_FALSE(std::filesystem::exists(old_orphan));
    EXPECT_TRUE(std::filesystem::exists(recent));
}

}  // namespace
