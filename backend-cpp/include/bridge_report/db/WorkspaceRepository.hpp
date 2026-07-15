#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <drogon/orm/DbClient.h>

#include "bridge_report/archive/WordInputArchive.hpp"
#include "bridge_report/review/WorkspaceModels.hpp"

namespace bridge_report::db {

enum class CreateInspectionYearStatus {
    Created,
    AlreadyExists,
    BridgeNotFound,
};

struct CreateInspectionYearOutcome {
    CreateInspectionYearStatus status{CreateInspectionYearStatus::BridgeNotFound};
    std::optional<review::WorkspaceInspection> inspection_year;
    std::optional<std::string> existing_inspection_year_id;
};

enum class UploadWordStatus {
    Created,
    InspectionYearNotFound,
    InspectionYearNotCurrent,
    ArchiveFailed,
};

struct UploadWordOutcome {
    UploadWordStatus status{UploadWordStatus::ArchiveFailed};
    std::optional<review::WorkspaceImport> import_record;
};

class WorkspaceRepository {
public:
    explicit WorkspaceRepository(drogon::orm::DbClientPtr db_client);

    std::optional<review::BridgeOverview> get_bridge_overview(const std::string& bridge_id);
    std::optional<review::InspectionWorkspace> get_inspection_workspace(const std::string& inspection_year_id);
    CreateInspectionYearOutcome create_inspection_year(const std::string& bridge_id, int inspection_year);
    UploadWordOutcome upload_word_import(
        const std::string& inspection_year_id,
        const std::string& source_type,
        const archive::WordInputMetadata& metadata,
        std::string_view content,
        const std::filesystem::path& archive_root
    );

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
