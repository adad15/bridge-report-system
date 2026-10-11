#include "bridge_report/db/DeadlockRetry.hpp"

#include <array>
#include <cstddef>
#include <thread>

namespace bridge_report::db {

bool is_deadlock_error(const std::string_view message) noexcept {
    // PQerrorMessage 的首行是“严重级别:  主消息”，DETAIL / HINT / CONTEXT 在后面几行。
    const auto first_line = message.substr(0, message.find('\n'));
    // 依次是英文服务器、lc_messages 为中文的服务器（本机即是）报的主消息。
    constexpr std::array<std::string_view, 2> phrases{"deadlock detected", "检测到死锁"};
    for (const auto phrase : phrases) {
        if (first_line.find(phrase) != std::string_view::npos) return true;
    }
    return false;
}

std::optional<std::chrono::milliseconds> deadlock_retry_delay(const int retries_done) noexcept {
    constexpr std::array<std::chrono::milliseconds, 3> delays{
        std::chrono::milliseconds(50), std::chrono::milliseconds(100), std::chrono::milliseconds(200)};
    if (retries_done < 0 || retries_done >= static_cast<int>(delays.size())) return std::nullopt;
    return delays[static_cast<std::size_t>(retries_done)];
}

void sleep_before_deadlock_retry(const std::chrono::milliseconds delay) {
    std::this_thread::sleep_for(delay);
}

}  // namespace bridge_report::db
