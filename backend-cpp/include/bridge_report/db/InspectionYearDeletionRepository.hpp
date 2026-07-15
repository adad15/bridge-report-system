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
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
