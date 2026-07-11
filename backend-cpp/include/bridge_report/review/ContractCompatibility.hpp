#pragma once

#include <string_view>

#include <json/value.h>

namespace bridge_report::review {

enum class ContractCompatibility {
    Native11,
    Upgraded10,
    LegacyReadOnly,
};

struct ContractCompatibilityResult {
    Json::Value data;
    ContractCompatibility compatibility{ContractCompatibility::Native11};
};

ContractCompatibilityResult normalize_review_contract(
    Json::Value data,
    std::string_view import_status
);

std::string_view contract_compatibility_name(ContractCompatibility value);

}  // namespace bridge_report::review
