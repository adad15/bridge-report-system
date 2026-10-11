#include <chrono>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <drogon/orm/Exception.h>
#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/DeadlockRetry.hpp"

namespace {

using bridge_report::db::deadlock_retry_delay;
using bridge_report::db::is_deadlock_error;
using bridge_report::db::run_with_deadlock_retry;
using std::chrono::milliseconds;

// drogon 抛出的是带 PQerrorMessage 全文的 Failure：首行“严重级别:  主消息”，后面跟 DETAIL 等。
const std::string kChineseDeadlock =
    "错误:  检测到死锁\n"
    "DETAIL:  进程 1234 等待在事务 5678 上的 ShareLock; 由进程 4321 阻塞.\n"
    "HINT:  详细信息请查看服务器日志.\n";
const std::string kEnglishDeadlock =
    "ERROR:  deadlock detected\n"
    "DETAIL:  Process 1234 waits for ShareLock on transaction 5678; blocked by process 4321.\n";

}  // namespace

TEST(DeadlockRetryTest, RecognizesDeadlockInEnglishAndChineseServerMessages) {
    EXPECT_TRUE(is_deadlock_error(kChineseDeadlock));
    EXPECT_TRUE(is_deadlock_error(kEnglishDeadlock));
}

TEST(DeadlockRetryTest, DoesNotTreatOtherDatabaseErrorsAsDeadlock) {
    EXPECT_FALSE(is_deadlock_error("错误:  重复键违反唯一约束\"bridges_pkey\"\n"));
    EXPECT_FALSE(is_deadlock_error("ERROR:  canceling statement due to lock timeout\n"));
    EXPECT_FALSE(is_deadlock_error("server closed the connection unexpectedly\n"));
    EXPECT_FALSE(is_deadlock_error(""));
    // 只认首行的主消息：DETAIL 里的键值碰巧是这几个字，不能把唯一约束冲突当成死锁去重试。
    EXPECT_FALSE(is_deadlock_error(
        "ERROR:  duplicate key value violates unique constraint \"bridges_name_key\"\n"
        "DETAIL:  Key (bridge_name)=(deadlock detected) already exists.\n"));
}

TEST(DeadlockRetryTest, RetriesAtMostThreeTimesWithGrowingDelays) {
    EXPECT_EQ(deadlock_retry_delay(0), milliseconds(50));
    EXPECT_EQ(deadlock_retry_delay(1), milliseconds(100));
    EXPECT_EQ(deadlock_retry_delay(2), milliseconds(200));
    EXPECT_FALSE(deadlock_retry_delay(3).has_value());
    EXPECT_FALSE(deadlock_retry_delay(-1).has_value());
}

TEST(DeadlockRetryTest, ReturnsTheFirstResultWithoutWaiting) {
    std::vector<milliseconds> waits;
    int calls = 0;
    const auto result = run_with_deadlock_retry(
        "test", [&] { return ++calls; }, [&](milliseconds delay) { waits.push_back(delay); });
    EXPECT_EQ(result, 1);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(waits.empty());
}

TEST(DeadlockRetryTest, RerunsTheWholeAttemptAfterADeadlock) {
    std::vector<milliseconds> waits;
    int calls = 0;
    const auto result = run_with_deadlock_retry(
        "test",
        [&] {
            if (++calls <= 2) throw drogon::orm::Failure(kChineseDeadlock);
            return calls;
        },
        [&](milliseconds delay) { waits.push_back(delay); });
    EXPECT_EQ(result, 3);
    EXPECT_EQ(waits, (std::vector<milliseconds>{milliseconds(50), milliseconds(100)}));
}

TEST(DeadlockRetryTest, GivesUpAfterThreeRetriesAndRethrowsTheDeadlock) {
    std::vector<milliseconds> waits;
    int calls = 0;
    EXPECT_THROW(
        run_with_deadlock_retry(
            "test",
            [&]() -> int {
                ++calls;
                throw drogon::orm::Failure(kEnglishDeadlock);
            },
            [&](milliseconds delay) { waits.push_back(delay); }),
        drogon::orm::Failure);
    EXPECT_EQ(calls, 4);
    EXPECT_EQ(waits, (std::vector<milliseconds>{milliseconds(50), milliseconds(100), milliseconds(200)}));
}

TEST(DeadlockRetryTest, DoesNotRetryOtherErrors) {
    std::vector<milliseconds> waits;
    int calls = 0;
    EXPECT_THROW(
        run_with_deadlock_retry(
            "test",
            [&]() -> int {
                ++calls;
                throw drogon::orm::Failure("错误:  重复键违反唯一约束\"bridges_pkey\"\n");
            },
            [&](milliseconds delay) { waits.push_back(delay); }),
        drogon::orm::Failure);
    EXPECT_EQ(calls, 1);

    // 非数据库异常哪怕文字一样也不算：只有数据库驱动抛的异常才可能是服务器报的死锁。
    calls = 0;
    EXPECT_THROW(
        run_with_deadlock_retry(
            "test",
            [&]() -> int {
                ++calls;
                throw std::runtime_error(kEnglishDeadlock);
            },
            [&](milliseconds delay) { waits.push_back(delay); }),
        std::runtime_error);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(waits.empty());
}

// 制造一次真实死锁：两条独立连接各开一个事务，按相反顺序去拿同一对咨询锁。
// 服务器实际报出来的那条消息必须被 is_deadlock_error 认出来。哪天服务器换了
// lc_messages（比如换成日文），这条会先红，而不是让删除的死锁重试悄悄失效。
TEST(DeadlockRetryDatabaseTest, RecognizesTheDeadlockMessageThisServerActuallySends) {
    if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) GTEST_SKIP();
    const bridge_report::config::PostgresConfig config;
    auto first_client = bridge_report::db::create_db_client(config, 1);
    auto second_client = bridge_report::db::create_db_client(config, 1);
    auto probe_client = bridge_report::db::create_db_client(config, 1);
    // 兜底：万一等待环没形成，语句 8 秒后自己超时退出，不会把整轮测试挂住。
    first_client->execSqlSync("set statement_timeout = '8000'");
    second_client->execSqlSync("set statement_timeout = '8000'");

    // 咨询锁按数据库生效、与 schema 无关；20261011 这个键空间业务代码不用。
    auto first = first_client->newTransaction();
    auto second = second_client->newTransaction();
    first->execSqlSync("select pg_advisory_xact_lock(20261011, 1)");
    second->execSqlSync("select pg_advisory_xact_lock(20261011, 2)");

    std::optional<std::string> first_error;
    std::thread first_waiter([&] {
        try {
            first->execSqlSync("select pg_advisory_xact_lock(20261011, 2)");
        } catch (const drogon::orm::DrogonDbException& error) {
            first_error = error.base().what();
        }
    });
    // 等 first 真的排上 2 号锁的队，再让 second 反过来去拿 1 号锁，闭合等待环。
    bool first_waiting = false;
    for (int attempt = 0; attempt < 200 && !first_waiting; ++attempt) {
        first_waiting = probe_client->execSqlSync(
            "select count(*) as n from pg_locks "
            "where locktype='advisory' and classid=20261011 and objid=2 and not granted"
        )[0]["n"].as<int>() > 0;
        if (!first_waiting) std::this_thread::sleep_for(milliseconds(20));
    }
    std::optional<std::string> second_error;
    if (first_waiting) {
        try {
            second->execSqlSync("select pg_advisory_xact_lock(20261011, 1)");
        } catch (const drogon::orm::DrogonDbException& error) {
            second_error = error.base().what();
        }
    }
    // 被选为牺牲者的一方事务中止、锁随之释放，另一方拿到锁返回；收尾两边都回滚。
    second->rollback();
    first_waiter.join();
    first->rollback();

    ASSERT_TRUE(first_waiting) << "first 没有排上 2 号锁的队，等待环没有形成";
    ASSERT_NE(first_error.has_value(), second_error.has_value()) << "应当恰好有一方被选为死锁牺牲者";
    const auto& message = first_error.has_value() ? *first_error : *second_error;
    EXPECT_TRUE(is_deadlock_error(message)) << "服务器的死锁报错没被认出来：" << message;

    // 牺牲者那条连接回滚之后还能开新事务——整段重来靠的就是这一点。
    for (const auto& client : {first_client, second_client}) {
        auto again = client->newTransaction();
        EXPECT_EQ(again->execSqlSync("select 1 as one")[0]["one"].as<int>(), 1);
        again->rollback();
    }
    first_client->closeAll();
    second_client->closeAll();
    probe_client->closeAll();
}
