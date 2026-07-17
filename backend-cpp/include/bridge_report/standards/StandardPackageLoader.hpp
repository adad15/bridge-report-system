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

class StandardPackageLoader {
public:
    static constexpr int supported_contract_version = 1;

    StandardChecksumResult calculate_checksum(const std::filesystem::path& package_root) const;
    StandardLoadResult load(const std::filesystem::path& package_root) const;
    std::vector<std::filesystem::path> discover(const std::filesystem::path& standards_root) const;
};

}  // namespace bridge_report::standards
