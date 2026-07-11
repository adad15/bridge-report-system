#include "bridge_report/review/ContractCompatibility.hpp"

#include <utility>

namespace bridge_report::review {

ContractCompatibilityResult normalize_review_contract(
    Json::Value data,
    std::string_view import_status
) {
    const bool is_10 = data.isObject() && data["contract"].isObject()
        && data["contract"]["version"].isString()
        && data["contract"]["version"].asString() == "1.0";
    if (!is_10) {
        return {std::move(data), ContractCompatibility::Native11};
    }

    data["contract"]["version"] = "1.1";
    if (data["defects"].isArray()) {
        for (auto& defect : data["defects"]) {
            if (!defect.isObject()) {
                continue;
            }
            if (!defect.isMember("group_review_status")) {
                defect["group_review_status"] = "待确认";
            }
            if (!defect.isMember("confirmed_missing_photo_numbers")) {
                defect["confirmed_missing_photo_numbers"] = Json::Value(Json::arrayValue);
            }
        }
    }

    const auto compatibility = import_status == "待校对"
        ? ContractCompatibility::Upgraded10
        : ContractCompatibility::LegacyReadOnly;
    return {std::move(data), compatibility};
}

std::string_view contract_compatibility_name(ContractCompatibility value) {
    switch (value) {
    case ContractCompatibility::Native11:
        return "native_1_1";
    case ContractCompatibility::Upgraded10:
        return "upgraded_1_0";
    case ContractCompatibility::LegacyReadOnly:
        return "legacy_read_only";
    }
    return "native_1_1";
}

}  // namespace bridge_report::review
