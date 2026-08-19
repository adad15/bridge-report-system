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
    EditLockInvalid,  // 编辑锁在写事务内已失效
    NotFound,
    Conflict,
    Invalid,
    Stale,
    Failed,
};

struct ComponentRangeSplitOutcome {
    ComponentRangeSplitStatus status{ComponentRangeSplitStatus::Failed};
    std::optional<review::ComponentRangeSplitAnalysis> analysis;
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

    // 读操作：只校验 expected_revision_id 仍然有效，绝不锁定年度版本。
    // 打开一次预览就把年度锁死，是任何人都不会预期的副作用。
    [[nodiscard]] ComponentRangeSplitOutcome preview(
        const std::string& import_id,
        const std::vector<review::ComponentRangeSplitTarget>& targets,
        const std::string& expected_revision_id);

    // 写操作：事务内重新校验版本并在年度未锁定时锁定它。
    // expected_impact_token 管的是另一件事——预览之后病害数据或分析结果变了。
    // 两道闸门互不替代，必须都在。
    // edit_lock 与绑定写接口同一套：路由先拦一道，仓储在事务内复查。
    [[nodiscard]] ComponentRangeSplitOutcome apply(
        const std::string& import_id,
        const std::vector<review::ComponentRangeSplitTarget>& targets,
        const std::string& expected_impact_token,
        const std::string& user_id,
        const std::string& expected_revision_id,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
