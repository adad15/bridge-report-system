#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace bridge_report::inventory {

enum class ComponentRangeParseStatus {
    Ok,
    NotRange,
    Invalid,
    LimitExceeded,
};

struct ComponentRangeParseResult {
    ComponentRangeParseStatus status{ComponentRangeParseStatus::NotRange};
    std::string source;
    std::string first;
    std::string last;
    std::vector<std::string> numbers;
    std::string message;
};

/**
 * @brief 安全展开同前缀、同后缀、仅末位整数变化的构件范围。
 *
 * 支持半角/全角波浪号。编号比较复用 ComponentMatcher 的权威归一化，
 * 输出则保留起点的展示模板（含全角符号和数字补零宽度）。
 */
[[nodiscard]] ComponentRangeParseResult parse_component_range(
    const std::string& value,
    std::size_t max_count = 500
);

}  // namespace bridge_report::inventory
