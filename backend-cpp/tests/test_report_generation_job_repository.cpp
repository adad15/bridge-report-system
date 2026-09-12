#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ReportGenerationJobRepository.hpp"

namespace {

namespace fs = std::filesystem;

using bridge_report::db::JobCreation;
using bridge_report::db::ReportGenerationJobRepository;
using bridge_report::report::JobStatus;

/// 整个进程共用一个连接：max_connections 是 100，而每个用例各开一个 DbClient 会在
/// 全量跑时把连接池耗尽。
drogon::orm::DbClientPtr shared_test_client() {
    static drogon::orm::DbClientPtr client = [] {
        const bridge_report::config::PostgresConfig config{};
        return bridge_report::db::create_db_client(config, 1);
    }();
    return client;
}

// 生成任务仓储的数据库集成夹具（设计 §17）。
//
// 测的都是"不能交给调用方自觉遵守"的规则：重复提交只能有一个进行中任务、终态不
// 会被迟到的回调改回去、到期清理必须同时删文件和清路径、删文件只准删受控目录里的。
class ReportGenerationJobRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();
        suffix_ = std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) +
                  "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed());

        user_id_ = insert_returning_id(
            "insert into users (username, display_name, password_hash, role) "
            "values ($1, '任务测试用户', 'not-a-real-hash', 'admin') returning id",
            "job_user_" + suffix_);
        other_user_id_ = insert_returning_id(
            "insert into users (username, display_name, password_hash, role) "
            "values ($1, '任务测试用户2', 'not-a-real-hash', 'normal') returning id",
            "job_user2_" + suffix_);
        bridge_id_ = insert_returning_id(
            "insert into bridges (bridge_name) values ('任务测试桥') returning id");
        year_id_ = insert_returning_id(
            "insert into inspection_years (bridge_id, inspection_year, status) "
            "values ($1::uuid, 2026, '已确认') returning id",
            bridge_id_);

        job_root_ = fs::temp_directory_path() / ("bridge-report-jobs-" + suffix_);
        fs::create_directories(job_root_);
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        client_->execSqlSync(
            "delete from report_generation_jobs where inspection_year_id=$1::uuid", year_id_);
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", year_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from users where id=$1::uuid", user_id_);
        client_->execSqlSync("delete from users where id=$1::uuid", other_user_id_);
        std::error_code error;
        fs::remove_all(job_root_, error);
    }

    template <typename... Args>
    std::string insert_returning_id(const std::string& sql, Args&&... args) {
        const auto result = client_->execSqlSync(sql, std::forward<Args>(args)...);
        return result[0]["id"].template as<std::string>();
    }

    ReportGenerationJobRepository repository() {
        return ReportGenerationJobRepository(client_);
    }

    /// 在任务目录里造一份成品，返回它的路径。
    fs::path make_output(const std::string& job_id) {
        const auto directory = job_root_ / job_id;
        fs::create_directories(directory);
        const auto path = directory / "report.docx";
        std::ofstream(path, std::ios::binary) << "docx";
        return path;
    }

    /// 把到期时间拨到过去，模拟保留期已过。
    void expire_now(const std::string& job_id) {
        client_->execSqlSync(
            "update report_generation_jobs set expires_at = now() - interval '1 hour' "
            "where id=$1::uuid",
            job_id);
    }

    drogon::orm::DbClientPtr client_;
    std::string suffix_;
    std::string user_id_;
    std::string other_user_id_;
    std::string bridge_id_;
    std::string year_id_;
    fs::path job_root_;
};

TEST_F(ReportGenerationJobRepositoryTest, CreateStartsAQueuedJob) {
    const auto created = repository().create(year_id_, user_id_, std::nullopt, std::nullopt);

    ASSERT_EQ(created.outcome, JobCreation::Outcome::Created);
    ASSERT_TRUE(created.job.has_value());
    EXPECT_EQ(created.job->status, JobStatus::Queued);
    EXPECT_EQ(created.job->inspection_year_id, year_id_);
    EXPECT_FALSE(created.job->can_download());
}

TEST_F(ReportGenerationJobRepositoryTest, YearMustExist) {
    const auto created = repository().create(
        "00000000-0000-0000-0000-000000000000", user_id_, std::nullopt, std::nullopt);

    EXPECT_EQ(created.outcome, JobCreation::Outcome::YearNotFound);
    EXPECT_FALSE(created.job.has_value());
}

// 设计 §17.3：重复提交返回已有任务，不再起一个。起第二个会让两份 Word 抢同一台机器。
TEST_F(ReportGenerationJobRepositoryTest, SubmittingAgainReturnsTheRunningJob) {
    auto repo = repository();
    const auto first = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(first.job.has_value());

    const auto second = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);

    EXPECT_EQ(second.outcome, JobCreation::Outcome::AlreadyRunning);
    ASSERT_TRUE(second.job.has_value());
    EXPECT_EQ(second.job->id, first.job->id);
}

// 限制是"同一用户同一年度"，不是"同一年度"：两个人各自生成一份是正常需求。
TEST_F(ReportGenerationJobRepositoryTest, AnotherUserGetsTheirOwnJob) {
    auto repo = repository();
    const auto mine = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    const auto theirs = repo.create(year_id_, other_user_id_, std::nullopt, std::nullopt);

    ASSERT_TRUE(mine.job.has_value());
    ASSERT_TRUE(theirs.job.has_value());
    EXPECT_EQ(theirs.outcome, JobCreation::Outcome::Created);
    EXPECT_NE(theirs.job->id, mine.job->id);
}

TEST_F(ReportGenerationJobRepositoryTest, AFinishedJobDoesNotBlockTheNextOne) {
    auto repo = repository();
    const auto first = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(first.job.has_value());
    ASSERT_TRUE(repo.mark_failed(first.job->id, "report_assemble_failed", "装配失败"));

    const auto second = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);

    EXPECT_EQ(second.outcome, JobCreation::Outcome::Created);
    ASSERT_TRUE(second.job.has_value());
    EXPECT_NE(second.job->id, first.job->id);
}

TEST_F(ReportGenerationJobRepositoryTest, AdvanceMovesThroughTheStateMachine) {
    auto repo = repository();
    const auto created = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(created.job.has_value());
    const auto id = created.job->id;

    Json::Value progress;
    progress["stage"] = "装配";
    ASSERT_TRUE(repo.advance(id, JobStatus::AssemblingDocx, progress));

    const auto reloaded = repo.find(id);
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->status, JobStatus::AssemblingDocx);
    EXPECT_EQ(reloaded->progress["stage"].asString(), "装配");
}

// 一个迟到的阶段回调不能把已经失败的任务改回处理中，更不能把它变成 ready。
TEST_F(ReportGenerationJobRepositoryTest, AFinalJobIsNeverMovedBackByALateCallback) {
    auto repo = repository();
    const auto created = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(created.job.has_value());
    const auto id = created.job->id;
    ASSERT_TRUE(repo.mark_failed(id, "report_field_update_failed", "Word 没响应"));

    EXPECT_FALSE(repo.advance(id, JobStatus::ValidatingDocx, Json::Value(Json::objectValue)));
    EXPECT_FALSE(repo.mark_ready(id, make_output(id), "report.docx", 24,
                                Json::Value(Json::objectValue)));

    const auto reloaded = repo.find(id);
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->status, JobStatus::Failed);
    EXPECT_EQ(reloaded->error_code.value_or(""), "report_field_update_failed");
}

TEST_F(ReportGenerationJobRepositoryTest, MarkReadyRecordsTheFileAndItsExpiry) {
    auto repo = repository();
    const auto created = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(created.job.has_value());
    const auto id = created.job->id;
    const auto output = make_output(id);

    Json::Value progress;
    progress["page_count"] = 101;
    ASSERT_TRUE(repo.mark_ready(id, output, "百股大桥定期检测报告（2类）.docx", 24, progress));

    const auto reloaded = repo.find(id);
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_TRUE(reloaded->can_download());
    EXPECT_EQ(reloaded->download_filename.value_or(""), "百股大桥定期检测报告（2类）.docx");
    EXPECT_TRUE(reloaded->expires_at.has_value());
    EXPECT_TRUE(reloaded->finished_at.has_value());
    EXPECT_EQ(reloaded->progress["page_count"].asInt(), 101);
}

// 设计 §17.1：到期删文件、转 expired、清空路径与诊断正文。
TEST_F(ReportGenerationJobRepositoryTest, CleanupRemovesTheFileAndClearsEverything) {
    auto repo = repository();
    const auto created = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(created.job.has_value());
    const auto id = created.job->id;
    const auto output = make_output(id);
    Json::Value progress;
    progress["page_count"] = 101;
    ASSERT_TRUE(repo.mark_ready(id, output, "报告.docx", 24, progress));
    expire_now(id);

    const auto summary = repo.cleanup(job_root_, 7);

    EXPECT_EQ(summary.expired, 1);
    EXPECT_EQ(summary.files_removed, 1);
    EXPECT_FALSE(fs::exists(output));
    const auto reloaded = repo.find(id);
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->status, JobStatus::Expired);
    EXPECT_FALSE(reloaded->temporary_file_path.has_value());
    EXPECT_FALSE(reloaded->download_filename.has_value());
    EXPECT_TRUE(reloaded->progress.empty());
}

TEST_F(ReportGenerationJobRepositoryTest, CleanupLeavesJobsThatHaveNotExpiredAlone) {
    auto repo = repository();
    const auto created = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(created.job.has_value());
    const auto id = created.job->id;
    const auto output = make_output(id);
    ASSERT_TRUE(repo.mark_ready(id, output, "报告.docx", 24, Json::Value(Json::objectValue)));

    const auto summary = repo.cleanup(job_root_, 7);

    EXPECT_EQ(summary.expired, 0);
    EXPECT_TRUE(fs::exists(output));
}

// 路径来自数据库，删之前必须自证它在受控根目录内。拿到什么删什么，一条被改过的
// 路径就能删掉任意目录。
TEST_F(ReportGenerationJobRepositoryTest, CleanupRefusesToDeleteOutsideTheControlledRoot) {
    auto repo = repository();
    const auto created = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(created.job.has_value());
    const auto id = created.job->id;

    const auto outside = fs::temp_directory_path() / ("bridge-report-outside-" + suffix_);
    fs::create_directories(outside);
    const auto stray = outside / "report.docx";
    std::ofstream(stray, std::ios::binary) << "docx";
    ASSERT_TRUE(repo.mark_ready(id, stray, "报告.docx", 24, Json::Value(Json::objectValue)));
    expire_now(id);

    const auto summary = repo.cleanup(job_root_, 7);

    EXPECT_EQ(summary.expired, 1) << "任务行照样要转 expired，不能因为文件删不掉就一直留着";
    EXPECT_EQ(summary.files_removed, 0);
    EXPECT_TRUE(fs::exists(stray)) << "受控目录之外的文件一个都不许碰";

    std::error_code error;
    fs::remove_all(outside, error);
}

// 终态行保留 7 天用于界面说明和排障，随后物理删除（设计 §17.1）。
TEST_F(ReportGenerationJobRepositoryTest, CleanupPurgesFinishedRowsPastTheRetentionWindow) {
    auto repo = repository();
    const auto created = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(created.job.has_value());
    const auto id = created.job->id;
    ASSERT_TRUE(repo.mark_failed(id, "report_preflight_blocked", "缺正式评定"));
    client_->execSqlSync(
        "update report_generation_jobs set finished_at = now() - interval '30 days' "
        "where id=$1::uuid",
        id);

    const auto summary = repo.cleanup(job_root_, 7);

    EXPECT_EQ(summary.purged, 1);
    EXPECT_FALSE(repo.find(id).has_value());
}

// 「当前报告」永远是最近那一次，不管上一次是成功还是失败。系统不保存报告版本，
// 界面上也就没有历史可翻。
TEST_F(ReportGenerationJobRepositoryTest, CurrentJobIsTheMostRecentOne) {
    auto repo = repository();
    const auto first = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(first.job.has_value());
    ASSERT_TRUE(repo.mark_failed(first.job->id, "report_assemble_failed", "失败"));
    const auto second = repo.create(year_id_, user_id_, std::nullopt, std::nullopt);
    ASSERT_TRUE(second.job.has_value());

    const auto current = repo.find_current(year_id_);

    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->id, second.job->id);
}

TEST_F(ReportGenerationJobRepositoryTest, AYearThatNeverGeneratedHasNoCurrentJob) {
    EXPECT_FALSE(repository().find_current(year_id_).has_value());
}

}  // namespace
