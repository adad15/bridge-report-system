#pragma once

#include <compare>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json/json.h>

namespace bridge_report::standards {

enum class StandardFamily {
    technical_condition,
    maintenance,
};

std::string to_string(StandardFamily family);
std::optional<StandardFamily> parse_standard_family(std::string_view value);

struct StandardPackageKey {
    StandardFamily family{StandardFamily::technical_condition};
    std::string standard_id;
    std::string package_version;

    auto operator<=>(const StandardPackageKey&) const = default;
};

struct StandardManifest {
    StandardFamily family{StandardFamily::technical_condition};
    std::string standard_id;
    std::string standard_code;
    std::string standard_name;
    std::string official_edition;
    std::string package_version;
    int contract_version{0};
    std::string algorithm_id;
    std::string effective_date;
    std::string content_checksum;
    std::string status;
    std::vector<std::string> entry_files;
};

struct StandardDefinition {
    std::string id;
    std::vector<std::string> references;
    std::string source_file;
    Json::Value payload;
};

struct StandardPackage {
    StandardManifest manifest;
    std::map<std::string, Json::Value> documents;
    std::map<std::string, StandardDefinition> definitions;

    StandardPackageKey key() const;
};

struct StandardIssue {
    std::string code;
    std::string message;
};

}  // namespace bridge_report::standards
