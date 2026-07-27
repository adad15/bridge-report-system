#include "bridge_report/review/ContractCompatibility.hpp"

#include <string>
#include <utility>

namespace bridge_report::review {
namespace {

constexpr const char* kMatchRequired = "defect_component_match_required";
constexpr const char* kMatchAmbiguous = "defect_component_match_ambiguous";

std::string contract_version_of(const Json::Value& data) {
    if (data.isObject() && data["contract"].isObject() && data["contract"]["version"].isString()) {
        return data["contract"]["version"].asString();
    }
    return {};
}

bool non_empty_string_member(const Json::Value& value, const char* key) {
    return value.isObject() && value[key].isString() && !value[key].asString().empty();
}

bool is_component_match_warning(const Json::Value& warning) {
    if (!warning.isObject() || !warning["code"].isString()) return false;
    const auto code = warning["code"].asString();
    return code == kMatchRequired || code == kMatchAmbiguous;
}

}  // namespace

void reconcile_defect_component_match_warning(Json::Value& defect) {
    if (!defect.isObject()) return;

    Json::Value warnings(Json::arrayValue);
    if (defect["warnings"].isArray()) {
        for (const auto& warning : defect["warnings"]) {
            if (!is_component_match_warning(warning)) warnings.append(warning);
        }
    }

    const bool resolved =
        non_empty_string_member(defect, "bridge_component_id")
        || (defect["component_match_method"].isString()
            && defect["component_match_method"].asString() == "missing")
        || (defect["review_status"].isString()
            && defect["review_status"].asString() == "已忽略");
    if (!resolved) {
        const bool ambiguous =
            defect["component_match_candidate_ids"].isArray()
            && !defect["component_match_candidate_ids"].empty();
        Json::Value warning(Json::objectValue);
        warning["code"] = ambiguous ? kMatchAmbiguous : kMatchRequired;
        warning["message"] = ambiguous
            ? "存在构件匹配候选，请人工确认实际构件。"
            : "未找到可唯一关联的实际构件，请人工选择。";
        warning["severity"] = "warning";
        warning["target_candidate_id"] =
            non_empty_string_member(defect, "candidate_id")
            ? defect["candidate_id"] : Json::Value();
        warnings.append(std::move(warning));
    }
    defect["warnings"] = std::move(warnings);
}

void reconcile_component_match_warnings(Json::Value& data) {
    if (!data["defects"].isArray()) return;
    for (auto& defect : data["defects"]) {
        reconcile_defect_component_match_warning(defect);
    }
}

ContractCompatibilityResult normalize_review_contract(
    Json::Value data,
    std::string_view import_status
) {
    (void)import_status;
    reconcile_component_match_warnings(data);
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
