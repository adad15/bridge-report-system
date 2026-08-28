#include "bridge_report/resolution/EffectiveDefectFacts.hpp"

#include <algorithm>
#include <unordered_set>

namespace bridge_report::resolution {
namespace {

// 必填事实：覆盖里出现就必须是字符串，写 null 等于用覆盖把来源合同不接受的值送进
// 预检和正式事实。"清除覆盖"是删键。
const std::unordered_set<std::string>& required_string_fields() {
    static const std::unordered_set<std::string> values = {
        "defect_type", "defect_location", "defect_description"};
    return values;
}

const std::unordered_set<std::string>& nullable_string_fields() {
    static const std::unordered_set<std::string> values = {
        "quantity_text", "measurement_text", "remark"};
    return values;
}

}  // namespace

const std::vector<std::string>& overridable_fact_fields() {
    static const std::vector<std::string> fields = {
        "defect_type",
        "defect_location",
        "defect_scale",
        "defect_description",
        "quantity_text",
        "measurement_text",
        "measurements",
        "remark",
    };
    return fields;
}

bool fact_overrides_are_valid(const Json::Value& overrides, std::string& reason) {
    if (!overrides.isObject()) {
        reason = "fact_overrides_json must be an object";
        return false;
    }
    const auto& allowed = overridable_fact_fields();
    for (const auto& key : overrides.getMemberNames()) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            reason = "field is not overridable: " + key;
            return false;
        }
        const auto& value = overrides[key];
        if (required_string_fields().contains(key)) {
            if (!value.isString()) {
                reason = "field must be a non-null string: " + key;
                return false;
            }
            continue;
        }
        if (nullable_string_fields().contains(key)) {
            if (!value.isNull() && !value.isString()) {
                reason = "field must be a string or null: " + key;
                return false;
            }
            continue;
        }
        if (key == "measurements") {
            if (!value.isArray()) {
                reason = "measurements must be an array";
                return false;
            }
            continue;
        }
        if (key == "defect_scale") {
            if (value.isNull()) continue;
            if (!value.isIntegral() || value.asInt64() <= 0) {
                reason = "defect_scale must be a positive integer or null";
                return false;
            }
            continue;
        }
    }
    return true;
}

Json::Value merge_effective_defect_facts(
    const Json::Value& source_defect, const Json::Value& fact_overrides) {
    Json::Value effective = source_defect;
    if (!fact_overrides.isObject()) return effective;
    for (const auto& key : fact_overrides.getMemberNames()) {
        const auto& allowed = overridable_fact_fields();
        // 白名单之外的键在写入时就该被挡住；这里再挡一次，避免任何一条旁路把
        // 构件解析或评分树字段经由覆盖 JSON 混回有效事实。
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) continue;
        effective[key] = fact_overrides[key];
    }
    return effective;
}

std::vector<std::string> overridden_fact_fields(const Json::Value& fact_overrides) {
    std::vector<std::string> fields;
    if (!fact_overrides.isObject()) return fields;
    for (const auto& key : overridable_fact_fields()) {
        if (fact_overrides.isMember(key)) fields.push_back(key);
    }
    return fields;
}

}  // namespace bridge_report::resolution
