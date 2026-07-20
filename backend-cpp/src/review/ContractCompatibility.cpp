#include "bridge_report/review/ContractCompatibility.hpp"

#include <string>
#include <utility>

namespace bridge_report::review {
namespace {

std::string contract_version_of(const Json::Value& data) {
    if (data.isObject() && data["contract"].isObject() && data["contract"]["version"].isString()) {
        return data["contract"]["version"].asString();
    }
    return {};
}

}  // namespace

ContractCompatibilityResult normalize_review_contract(
    Json::Value data,
    std::string_view import_status
) {
    (void)import_status;
    return {std::move(data), ContractCompatibility::Native20};
}

std::string_view contract_compatibility_name(ContractCompatibility value) {
    switch (value) {
    case ContractCompatibility::Native20:
        return "native_2_0";
    }
    return "native_2_0";
}

bool stored_contract_requires_reparse(const Json::Value& stored_data) {
    return contract_version_of(stored_data) != "2.0";
}

}  // namespace bridge_report::review
