#include "bridge_report/runtime/RuntimePaths.hpp"

namespace bridge_report::runtime {

void ensure_log_directory(const std::filesystem::path& log_path) {
    std::filesystem::create_directories(log_path);
}

}  // namespace bridge_report::runtime
