#pragma once

#include "bridge_report/standards/MaintenanceStandard.hpp"

namespace bridge_report::standards {

class Jtg5120MaintenanceStandard final : public MaintenanceStandard {
public:
    explicit Jtg5120MaintenanceStandard(StandardPackage package);

    std::string algorithm_id() const override;
    const StandardManifest& metadata() const noexcept override;
    MaintenanceLevelsResult maintenance_levels() const override;
    InspectionTypesResult inspection_types() const override;
    PeriodicInspectionRequirementResult periodic_inspection_requirements(
        const MaintenanceQueryContext& context) const override;
    ProjectRequirementValidation validate_project_requirements(
        const MaintenanceQueryContext& context) const override;

private:
    StandardPackage package_;
};

}  // namespace bridge_report::standards
