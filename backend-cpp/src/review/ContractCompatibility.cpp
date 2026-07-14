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
    const auto version = contract_version_of(data);
    // 只识别历史上真实存在过的旧版本；其余（含 1.2 与无法识别的形状）原样返回，
    // 交由严格校验器判定，避免把垃圾数据误标成旧版可读记录。
    if (version != "1.0" && version != "1.1") {
        return {std::move(data), ContractCompatibility::Native12};
    }

    // 仅内存展示规范化：补上前端 1.2 守卫要求的必填成员，绝不落库。
    data["contract"]["version"] = "1.2";
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
    if (data["ratings"].isObject() && !data["ratings"].isMember("component_ratings")) {
        data["ratings"]["component_ratings"] = Json::Value(Json::arrayValue);
    }

    const auto compatibility = import_status == "待校对"
        ? ContractCompatibility::LegacyPendingReparse
        : ContractCompatibility::LegacyReadOnly;
    return {std::move(data), compatibility};
}

std::string_view contract_compatibility_name(ContractCompatibility value) {
    switch (value) {
    case ContractCompatibility::Native12:
        return "native_1_2";
    case ContractCompatibility::LegacyPendingReparse:
        return "legacy_pending_reparse";
    case ContractCompatibility::LegacyReadOnly:
        return "legacy_read_only";
    }
    return "native_1_2";
}

bool stored_contract_requires_reparse(const Json::Value& stored_data) {
    return contract_version_of(stored_data) != "1.2";
}

}  // namespace bridge_report::review
