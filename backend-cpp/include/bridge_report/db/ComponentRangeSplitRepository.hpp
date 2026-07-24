#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/db/ImportBindingRepository.hpp"
#include "bridge_report/review/ComponentRangeSplitPlanner.hpp"

namespace bridge_report::db {

enum class ComponentRangeSplitStatus {
    Ok,
    NotFound,
    Conflict,
    Invalid,
    Stale,
    Failed,
};

struct ComponentRangeSplitOutcome {
    ComponentRangeSplitStatus status{ComponentRangeSplitStatus::Failed};
    std::optional<review::ComponentRangeSplitPlan> plan;
    std::optional<BindingOverview> overview;
    std::string impact_token;
    std::string error_code;
    std::string error_message;
    review::ComponentRangeSplitTarget rejected_target;
    std::string operation_id;
};

class ComponentRangeSplitRepository {
public:
    explicit ComponentRangeSplitRepository(drogon::orm::DbClientPtr db_client);

    [[nodiscard]] ComponentRangeSplitOutcome preview(
        const std::string& import_id,
        const std::vector<review::ComponentRangeSplitTarget>& targets);

    [[nodiscard]] ComponentRangeSplitOutcome apply(
        const std::string& import_id,
        const std::vector<review::ComponentRangeSplitTarget>& targets,
        const std::string& expected_impact_token,
        const std::string& user_id);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
