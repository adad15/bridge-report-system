#pragma once

#include <string>
#include <string_view>

namespace bridge_report::identity {

/**
 * @brief 格式化模块 02 的系统编号，例如 "QL-000001"。
 * @param prefix 受支持的业务前缀，例如 "QL" 或 "NDJC"。
 * @param sequence_value 正整数序列值，输出时补齐为 6 位。
 * @return "<前缀>-<6位序号>" 格式的编号。
 */
std::string format_system_number(std::string_view prefix, int sequence_value);

/**
 * @brief 判断前缀是否属于模块 02 定义的系统编号前缀集合。
 */
bool is_supported_system_number_prefix(std::string_view prefix);

}  // 命名空间 bridge_report::identity
