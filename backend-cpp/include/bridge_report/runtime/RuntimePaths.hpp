#pragma once

#include <filesystem>

namespace bridge_report::runtime {

void ensure_log_directory(const std::filesystem::path& log_path);

}  // namespace bridge_report::runtime
