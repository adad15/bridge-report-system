#include "bridge_report/standards/DefectIndicatorResolver.hpp"

#include <algorithm>

namespace bridge_report::standards {
namespace {

bool contains_string(const Json::Value& values, const std::string& expected) {
    if (!values.isArray()) return false;
    return std::any_of(values.begin(), values.end(), [&](const Json::Value& value) {
        return value.isString() && value.asString() == expected;
    });
}

}  // namespace

DefectIndicatorResolution resolve_defect_indicator(
    const StandardPackage& package,
    const std::string& indicator_id,
    const std::string& component_type_id,
    std::optional<int> scale) {
    DefectIndicatorResolution result;
    result.indicator_id = indicator_id;
    if (indicator_id.empty()) {
        result.status = DefectIndicatorResolutionStatus::indicator_required;
        return result;
    }

    const Json::Value* matched_indicator = nullptr;
    const Json::Value* applicable_component_ids = nullptr;
    for (const auto& [_, definition] : package.definitions) {
        const auto& payload = definition.payload;
        if (!payload["indicators"].isArray() ||
            !payload["applicable_component_ids"].isArray()) {
            continue;
        }
        for (const auto& indicator : payload["indicators"]) {
            if (indicator["id"].isString() &&
                indicator["id"].asString() == indicator_id) {
                matched_indicator = &indicator;
                applicable_component_ids = &payload["applicable_component_ids"];
                break;
            }
        }
        if (matched_indicator != nullptr) break;
    }

    if (matched_indicator == nullptr || applicable_component_ids == nullptr) {
        result.status = DefectIndicatorResolutionStatus::indicator_unknown;
        return result;
    }

    result.indicator_name = (*matched_indicator)["name"].asString();
    if ((*matched_indicator)["allowed_scales"].isArray()) {
        for (const auto& value : (*matched_indicator)["allowed_scales"]) {
            if (value.isInt()) result.allowed_scales.push_back(value.asInt());
        }
    }
    if (!contains_string(*applicable_component_ids, component_type_id)) {
        result.status =
            DefectIndicatorResolutionStatus::indicator_not_applicable;
        return result;
    }
    if (scale.has_value() &&
        std::find(result.allowed_scales.begin(), result.allowed_scales.end(), *scale) ==
            result.allowed_scales.end()) {
        result.status = DefectIndicatorResolutionStatus::scale_not_allowed;
        return result;
    }
    result.status = DefectIndicatorResolutionStatus::resolved;
    return result;
}

}  // namespace bridge_report::standards
