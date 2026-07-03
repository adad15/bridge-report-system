#pragma once

#include <filesystem>

namespace bridge_report::runtime {

/**
 * @brief 确保运行时日志目录存在。
 */
void ensure_log_directory(const std::filesystem::path& log_path);

}  // 命名空间 bridge_report::runtime
