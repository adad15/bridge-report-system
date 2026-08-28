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
    if (!defect.isObject() || !defect.isMember("warnings")) return;
    if (!defect["warnings"].isArray()) return;

    // 5.0："这条病害绑没绑上构件"的权威来源是解析关系表——预检读
    // confirmable_view 的 resolved/missing 集合，校对页读工作区读模型。所以这里
    // 只剔除存量警告，不再自己判定、也不再生成新警告。
    //
    // 判定那段原本读 `bridge_component_id` / `component_match_method` /
    // `component_match_candidate_ids`。这三个字段 5.0 已删，而 JsonCpp 非 const 的
    // operator[] 是会**建成员**的：读一下就往病害里插一个 null 键，于是
    // 响应里凭空多出被删字段，前端契约守卫整份拒掉。
    Json::Value kept(Json::arrayValue);
    for (const auto& warning : defect["warnings"]) {
        if (!is_component_match_warning(warning)) kept.append(warning);
    }
    defect["warnings"] = std::move(kept);
}

void reconcile_component_match_warnings(Json::Value& data) {
    if (!data.isObject() || !data.isMember("defects") || !data["defects"].isArray()) return;
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
    return {std::move(data), ContractCompatibility::Native50};
}

std::string_view contract_compatibility_name(ContractCompatibility value) {
    switch (value) {
    case ContractCompatibility::Native50:
        return "native_5_0";
    }
    return "native_5_0";
}

bool stored_contract_requires_reparse(const Json::Value& stored_data) {
    return contract_version_of(stored_data) != "5.0";
}

}  // namespace bridge_report::review
