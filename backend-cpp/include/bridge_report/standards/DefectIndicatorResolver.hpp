#pragma once

#include <optional>
#include <string>
#include <vector>

#include "bridge_report/standards/StandardModels.hpp"

namespace bridge_report::standards {

enum class DefectIndicatorResolutionStatus {
    resolved,
    indicator_required,
    indicator_unknown,
    indicator_not_applicable,
    scale_not_allowed,
};

struct DefectIndicatorResolution {
    DefectIndicatorResolutionStatus status{
        DefectIndicatorResolutionStatus::indicator_required};
    std::string indicator_id;
    std::string indicator_name;
    std::vector<int> allowed_scales;

    bool ok() const noexcept {
        return status == DefectIndicatorResolutionStatus::resolved;
    }
};

DefectIndicatorResolution resolve_defect_indicator(
    const StandardPackage& package,
    const std::string& indicator_id,
    const std::string& component_type_id,
    std::optional<int> scale = std::nullopt);

}  // namespace bridge_report::standards
