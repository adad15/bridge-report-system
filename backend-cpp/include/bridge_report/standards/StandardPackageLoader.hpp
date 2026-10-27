#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "bridge_report/standards/StandardModels.hpp"

namespace bridge_report::standards {

struct StandardChecksumResult {
    std::optional<std::string> checksum;
    std::optional<StandardIssue> issue;

    bool ok() const noexcept { return checksum.has_value(); }
};

struct StandardLoadResult {
    std::optional<StandardPackage> package;
    std::vector<StandardIssue> issues;

    bool ok() const noexcept { return package.has_value() && issues.empty(); }
};

// 运行时扫描的结果。草稿包（status 为 draft）只供测试和工具显式加载：
// 运行时不注册、不同步数据库、不进规则管理，所以单独列出、不算加载失败。
struct StandardRuntimeLoadResult {
    std::vector<StandardPackage> packages;
    std::vector<StandardManifest> skipped_drafts;
    std::vector<StandardIssue> issues;
};

class StandardPackageLoader {
public:
    static constexpr int supported_contract_version = 1;

    StandardChecksumResult calculate_checksum(const std::filesystem::path& package_root) const;
    StandardLoadResult load(const std::filesystem::path& package_root) const;
    std::vector<std::filesystem::path> discover(const std::filesystem::path& standards_root) const;
    // 后端启动和 report-context-dump 共用：跳过评定树目录（另有加载器）和草稿包。
    StandardRuntimeLoadResult load_runtime_packages(
        const std::filesystem::path& standards_root) const;
};

}  // namespace bridge_report::standards
