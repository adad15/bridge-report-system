#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/archive/ExtractedPhotoArchive.hpp"

namespace bridge_report::db {

struct WordImportContext {
    std::string import_record_id;
    std::string bridge_id;
    std::optional<std::string> inspection_year_id;
    std::string bridge_system_number;
    std::string bridge_name;
    int inspection_year{0};
    std::string import_record_system_number;
    std::string import_name;
    std::string source_type;
    std::string main_file_system_number;
    std::filesystem::path word_path;
};

struct PersistParseOutcome {
    bool success{false};
    std::string error_code;
    std::string error_message;
    std::vector<std::filesystem::path> obsolete_storage_paths;
};

class WordImportRepository {
public:
    explicit WordImportRepository(drogon::orm::DbClientPtr db_client);
    std::optional<WordImportContext> load_context(
        const std::string& import_record_id,
        const std::filesystem::path& archive_root
    );
    PersistParseOutcome persist_parse_result(
        const std::string& import_record_id,
        const archive::ArchivedPhotoBatch& batch
    );
    bool mark_parsing(const std::string& import_record_id);
    void mark_parse_failed(const std::string& import_record_id, const std::string& message);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
