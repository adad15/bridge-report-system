#pragma once

#include <optional>
#include <string>
#include <vector>

#include "bridge_report/standards/StandardRegistry.hpp"

namespace bridge_report::standards {

struct MaintenanceRuleSource {
    std::string rule_id;
    std::string source_reference;
};

struct MaintenanceRuleIssue {
    std::string code;
    std::string message;
    std::string field;
    std::string rule_id;
};

struct MaintenanceLevel {
    std::string id;
    std::string code;
    std::string name;
    MaintenanceRuleSource source;
};

struct InspectionType {
    std::string id;
    std::string code;
    std::string name;
    MaintenanceRuleSource source;
};

struct PeriodicInspectionContentGroup {
    std::string id;
    std::string name;
};

struct MaintenanceQueryContext {
    std::string maintenance_level_id;
    std::string inspection_type_id;
    std::optional<double> planned_interval_years;
};

template <typename T>
struct MaintenanceQueryResult {
    std::optional<T> value;
    std::vector<MaintenanceRuleIssue> issues;

    bool ok() const noexcept { return value.has_value() && issues.empty(); }
};

struct PeriodicInspectionRequirement {
    std::string standard_id;
    std::string package_version;
    std::string maintenance_level_id;
    std::string inspection_type_id;
    double maximum_interval_years{0.0};
    std::vector<PeriodicInspectionContentGroup> content_groups;
    std::vector<MaintenanceRuleSource> sources;
};

struct ProjectRequirementValidation {
    bool valid{false};
    std::vector<MaintenanceRuleIssue> issues;
    std::vector<MaintenanceRuleSource> sources;
};

using MaintenanceLevelsResult = MaintenanceQueryResult<std::vector<MaintenanceLevel>>;
using InspectionTypesResult = MaintenanceQueryResult<std::vector<InspectionType>>;
using PeriodicInspectionRequirementResult =
    MaintenanceQueryResult<PeriodicInspectionRequirement>;

class MaintenanceStandard : public StandardAlgorithmAdapter {
public:
    ~MaintenanceStandard() override = default;

    virtual const StandardManifest& metadata() const noexcept = 0;
    virtual MaintenanceLevelsResult maintenance_levels() const = 0;
    virtual InspectionTypesResult inspection_types() const = 0;
    virtual PeriodicInspectionRequirementResult periodic_inspection_requirements(
        const MaintenanceQueryContext& context) const = 0;
    virtual ProjectRequirementValidation validate_project_requirements(
        const MaintenanceQueryContext& context) const = 0;
};

}  // namespace bridge_report::standards
