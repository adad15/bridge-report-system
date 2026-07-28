#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "bridge_report/rating_tree/RatingTreeModels.hpp"

namespace bridge_report::rating_tree {

struct RatingTreeChecksumResult {
    std::optional<std::string> checksum;
    std::optional<RatingTreeIssue> issue;

    bool ok() const noexcept { return checksum.has_value() && !issue.has_value(); }
};

struct RatingTreeLoadResult {
    std::optional<RatingTreeExtensionPackage> package;
    std::vector<RatingTreeIssue> issues;

    bool ok() const noexcept { return package.has_value() && issues.empty(); }
};

class RatingTreePackageLoader {
public:
    static constexpr int supported_contract_version = 1;

    RatingTreeChecksumResult calculate_checksum(
        const std::filesystem::path& package_root) const;
    RatingTreeLoadResult load(const std::filesystem::path& package_root) const;
    std::vector<std::filesystem::path> discover(
        const std::filesystem::path& standards_root) const;
};

}  // namespace bridge_report::rating_tree
