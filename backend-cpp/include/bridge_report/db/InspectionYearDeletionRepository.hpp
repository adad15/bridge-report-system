#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/deletion/InspectionYearDeletionModels.hpp"

namespace bridge_report::db {

class InspectionYearDeletionRepository {
public:
    explicit InspectionYearDeletionRepository(drogon::orm::DbClientPtr db_client);

    std::optional<deletion::InspectionYearDeletionPlan> preview(
        const std::string& inspection_year_id
    ) const;

    deletion::DeleteInspectionYearOutcome delete_year(
        const std::string& inspection_year_id,
        const std::string& expected_impact_token,
        const std::string& confirmation_text,
        const std::string& reason,
        const deletion::DeletionActorSnapshot& actor
    );

private:
    // 一次完整尝试：开事务、检查、删除、等提交确认。出错先回滚再原样抛出，
    // 由 delete_year 决定是否因死锁整段重来。
    deletion::DeleteInspectionYearOutcome delete_year_once(
        const std::string& inspection_year_id,
        const std::string& expected_impact_token,
        const std::string& confirmation_text,
        const std::string& reason,
        const deletion::DeletionActorSnapshot& actor
    );

    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
