#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/deletion/ImportRecordDeletionModels.hpp"

namespace bridge_report::db {

class ImportRecordDeletionRepository {
public:
    explicit ImportRecordDeletionRepository(drogon::orm::DbClientPtr db_client);

    std::optional<deletion::ImportRecordDeletionPlan> preview(const std::string& import_record_id) const;
    deletion::DeleteImportRecordOutcome delete_import_record(
        const std::string& import_record_id,
        const std::string& expected_impact_token,
        const std::string& reason,
        const deletion::DeletionActorSnapshot& actor
    );

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
