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
    // 一次完整尝试：开事务、检查、删除、等提交确认。出错先回滚再原样抛出，
    // 由 delete_import_record 决定是否因死锁整段重来。
    deletion::DeleteImportRecordOutcome delete_import_record_once(
        const std::string& import_record_id,
        const std::string& expected_impact_token,
        const std::string& reason,
        const deletion::DeletionActorSnapshot& actor
    );

    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
