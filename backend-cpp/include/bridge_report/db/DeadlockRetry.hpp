#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <drogon/orm/Exception.h>
#include <trantor/utils/Logger.h>

namespace bridge_report::db {

/**
 * @brief 这条数据库报错是不是 PostgreSQL 检测到死锁、中止了本事务（SQLSTATE 40P01）。
 *
 * drogon 1.9 的 PG 驱动把所有数据库错误都包成只带 PQerrorMessage 全文的 Failure，
 * 拿不到 SQLSTATE，只能认服务器消息的首行：英文是 "deadlock detected"，lc_messages
 * 为中文时是 "检测到死锁"。只看首行，DETAIL 里碰巧出现这几个字（比如键值）不算。
 * 都没匹配上就当不是死锁，也就是不重试，和没有这层时一样。服务器换了语言要在这里
 * 补一种写法；DeadlockRetryDatabaseTest 会制造一次真实死锁，先把这件事报出来。
 */
bool is_deadlock_error(std::string_view message) noexcept;

/**
 * @brief 第 retries_done + 1 次重试前等多久；不该再重试时返回空。
 *
 * 最多重试 3 次，依次等 50、100、200 毫秒。死锁时数据库只中止一方，另一方随即拿到锁、
 * 通常很快提交；等一会儿是为了不让两边同时重来又撞在一起。
 */
std::optional<std::chrono::milliseconds> deadlock_retry_delay(int retries_done) noexcept;

/// 默认的等待方式：当前线程睡 delay。单独拿出来，测试可以换成只记录、不真睡。
void sleep_before_deadlock_retry(std::chrono::milliseconds delay);

/**
 * @brief 执行一次完整的事务 attempt；它因死锁抛出数据库异常时，等一会儿整段重来。
 *
 * 其余异常、以及重试用尽后的那次死锁异常原样抛给调用方，由调用方走原来的失败流程。
 *
 * attempt 必须满足：每次自己开事务，出错先回滚再抛；只在提交之前抛异常。提交结果经
 * CommitLatch 确认——提交失败或等待超时（结果不确定）由 attempt 以返回值表达、不走异常，
 * 所以不会被重试，同一次删除不会执行两遍。
 *
 * @param operation 写进重试日志的操作说明，例如 "bridge deletion bridge=<id>"。
 */
template <typename Attempt>
auto run_with_deadlock_retry(
    std::string_view operation,
    Attempt&& attempt,
    const std::function<void(std::chrono::milliseconds)>& wait = sleep_before_deadlock_retry
) -> decltype(attempt()) {
    for (int retries_done = 0;; ++retries_done) {
        try {
            return attempt();
        } catch (const drogon::orm::DrogonDbException& error) {
            if (!is_deadlock_error(error.base().what())) throw;
            const auto delay = deadlock_retry_delay(retries_done);
            if (!delay.has_value()) throw;
            LOG_WARN << std::string(operation) << " hit a deadlock; retry " << retries_done + 1
                     << " in " << delay->count() << "ms";
            wait(*delay);
        }
    }
}

}  // namespace bridge_report::db
