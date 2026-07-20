#include "bridge_report/db/WorkspaceRepository.hpp"

#include <memory>
#include <sstream>
#include <utility>

#include <json/json.h>
#include <trantor/utils/Logger.h>

#include "bridge_report/archive/TemporaryWordStorage.hpp"
#include "bridge_report/archive/WordInputArchive.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentArchiveRepository.hpp"
#include "bridge_report/review/ReviewStatistics.hpp"

namespace bridge_report::db {
namespace {

std::optional<std::string> optional_text(const drogon::orm::Row& row, const char* column) {
    if (row[column].isNull()) return std::nullopt;
    return row[column].as<std::string>();
}

std::optional<double> optional_double(const drogon::orm::Row& row, const char* column) {
    if (row[column].isNull()) return std::nullopt;
    return row[column].as<double>();
}

review::WorkspaceBridge bridge_from_row(const drogon::orm::Row& row) {
    review::WorkspaceBridge bridge;
    bridge.id = row["bridge_id"].as<std::string>();
    bridge.system_number = row["bridge_system_number"].as<std::string>();
    bridge.bridge_name = row["bridge_name"].as<std::string>();
    bridge.route_name = optional_text(row, "route_name");
    bridge.status = row["bridge_status"].as<std::string>();
    return bridge;
}

review::WorkspaceInspection inspection_from_row(const drogon::orm::Row& row) {
    review::WorkspaceInspection inspection;
    inspection.id = row["inspection_id"].as<std::string>();
    inspection.system_number = row["inspection_system_number"].as<std::string>();
    inspection.inspection_year = row["inspection_year"].as<int>();
    inspection.status = row["inspection_status"].as<std::string>();
    inspection.version_number = row["version_number"].as<int>();
    inspection.is_current = row["is_current"].as<bool>();
    inspection.overall_score = optional_double(row, "overall_score");
    inspection.overall_grade = optional_text(row, "overall_grade");
    inspection.created_at = optional_text(row, "inspection_created_at");
    inspection.updated_at = optional_text(row, "inspection_updated_at");
    return inspection;
}

review::WorkspaceStandardPackage standard_package_from_row(
    const drogon::orm::Row& row,
    const char* prefix,
    const char* family) {
    review::WorkspaceStandardPackage package;
    package.id = row[std::string(prefix) + "_package_id"].as<std::string>();
    package.family = family;
    package.standard_code = row[std::string(prefix) + "_standard_code"].as<std::string>();
    package.standard_name = row[std::string(prefix) + "_standard_name"].as<std::string>();
    package.official_edition = row[std::string(prefix) + "_official_edition"].as<std::string>();
    package.package_version = row[std::string(prefix) + "_package_version"].as<std::string>();
    package.is_enabled = row[std::string(prefix) + "_is_enabled"].as<bool>();
    package.sync_status = row[std::string(prefix) + "_sync_status"].as<std::string>();
    return package;
}

Json::Value parse_json_or_empty(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errors;
    std::istringstream input(text);
    if (!Json::parseFromStream(builder, input, &value, &errors) || !value.isObject()) {
        return Json::Value(Json::objectValue);
    }
    return value;
}

int pending_import_count(const drogon::orm::DbClientPtr& client, const std::string& bridge_id) {
    const auto rows = client->execSqlSync(
        "select count(*)::int as count from import_records ir "
        "left join inspection_years iy on iy.id = ir.inspection_year_id "
        "where ir.bridge_id = $1::uuid and ir.import_status in ('已上传','解析中','待校对','解析失败') "
        "and (ir.inspection_year_id is null or iy.is_current)", bridge_id);
    return rows[0]["count"].as<int>();
}

}  // namespace

WorkspaceRepository::WorkspaceRepository(drogon::orm::DbClientPtr db_client) : db_client_(std::move(db_client)) {}

std::optional<review::BridgeOverview> WorkspaceRepository::get_bridge_overview(const std::string& bridge_id) {
    const auto bridge_rows = db_client_->execSqlSync(
        "select id::text as bridge_id, system_number as bridge_system_number, bridge_name, route_name, "
        "status as bridge_status from bridges where id = $1::uuid", bridge_id);
    if (bridge_rows.empty()) return std::nullopt;

    review::BridgeOverview overview;
    overview.bridge = bridge_from_row(bridge_rows[0]);

    const auto inspection_rows = db_client_->execSqlSync(
        "select id::text as inspection_id, system_number as inspection_system_number, inspection_year, "
        "status as inspection_status, version_number, is_current, overall_score, overall_grade, "
        "created_at::text as inspection_created_at, updated_at::text as inspection_updated_at "
        "from inspection_years where bridge_id = $1::uuid and is_current "
        "and status in ('已确认','已归档') order by inspection_year desc limit 5", bridge_id);
    for (const auto& row : inspection_rows) {
        overview.recent_inspections.push_back(inspection_from_row(row));
    }
    if (!overview.recent_inspections.empty()) {
        overview.latest_inspection = overview.recent_inspections.front();
        const auto rating_rows = db_client_->execSqlSync(
            "select rating_level, rating_item_name, score, grade from condition_ratings "
            "where inspection_year_id = $1::uuid and assessment_run_id is not null "
            "and rating_level in ('结构分部','部件') "
            "and review_status in ('已确认','已修改') order by rating_level, rating_item_name",
            overview.latest_inspection->id);
        for (const auto& row : rating_rows) {
            review::WorkspaceStructureRating rating;
            rating.rating_level = row["rating_level"].as<std::string>();
            rating.rating_item_name = row["rating_item_name"].as<std::string>();
            rating.score = optional_double(row, "score");
            rating.grade = optional_text(row, "grade");
            overview.structure_ratings.push_back(std::move(rating));
        }
    }

    ComponentArchiveRepository archive_repository(db_client_);
    const auto component_body = archive_repository.list_components(bridge_id);
    const auto& components = component_body["components"];
    overview.defect_archive.component_count = components.isArray() ? static_cast<int>(components.size()) : 0;
    if (components.isArray()) {
        for (const auto& component : components) {
            overview.defect_archive.thread_count += component["thread_count"].asInt();
            overview.defect_archive.unbound_observation_count += component["unbound_count"].asInt();
        }
    }
    overview.pending.import_count = pending_import_count(db_client_, bridge_id);
    overview.pending.unbound_observation_count = overview.defect_archive.unbound_observation_count;
    return overview;
}

std::optional<review::InspectionWorkspace> WorkspaceRepository::get_inspection_workspace(
    const std::string& inspection_year_id
) {
    const auto context_rows = db_client_->execSqlSync(
        "select b.id::text as bridge_id, b.system_number as bridge_system_number, b.bridge_name, b.route_name, "
        "b.status as bridge_status, iy.id::text as inspection_id, "
        "iy.system_number as inspection_system_number, iy.inspection_year, iy.status as inspection_status, "
        "iy.version_number, iy.is_current, iy.overall_score, iy.overall_grade, "
        "iy.created_at::text as inspection_created_at, iy.updated_at::text as inspection_updated_at, "
        "sp.id::text as standard_profile_id, sp.revision_number as standard_profile_revision, "
        "sp.status as standard_profile_status, "
        "tp.id::text as technical_package_id, tp.standard_code as technical_standard_code, "
        "tp.standard_name as technical_standard_name, tp.official_edition as technical_official_edition, "
        "tp.package_version as technical_package_version, tp.is_enabled as technical_is_enabled, "
        "tp.sync_status as technical_sync_status, "
        "mp.id::text as maintenance_package_id, mp.standard_code as maintenance_standard_code, "
        "mp.standard_name as maintenance_standard_name, mp.official_edition as maintenance_official_edition, "
        "mp.package_version as maintenance_package_version, mp.is_enabled as maintenance_is_enabled, "
        "mp.sync_status as maintenance_sync_status "
        "from inspection_years iy join bridges b on b.id = iy.bridge_id "
        "left join project_standard_profiles sp on sp.id=iy.standard_profile_id "
        "left join standard_packages tp on tp.id=sp.technical_condition_package_id "
        "left join standard_packages mp on mp.id=sp.maintenance_package_id "
        "where iy.id = $1::uuid",
        inspection_year_id);
    if (context_rows.empty()) return std::nullopt;

    review::InspectionWorkspace workspace;
    workspace.bridge = bridge_from_row(context_rows[0]);
    workspace.inspection_year = inspection_from_row(context_rows[0]);
    if (!context_rows[0]["standard_profile_id"].isNull()) {
        review::WorkspaceStandardProfile profile;
        profile.id = context_rows[0]["standard_profile_id"].as<std::string>();
        profile.revision_number = context_rows[0]["standard_profile_revision"].as<int>();
        profile.status = context_rows[0]["standard_profile_status"].as<std::string>();
        profile.technical_condition = standard_package_from_row(
            context_rows[0], "technical", "technical_condition");
        profile.maintenance = standard_package_from_row(
            context_rows[0], "maintenance", "maintenance");
        workspace.standard_profile = std::move(profile);
    }

    const auto import_rows = db_client_->execSqlSync(
        "select ir.id::text, ir.system_number, ir.import_name, ir.source_type, ir.import_status, "
        "ir.importer_name, ir.parsed_result_json::text, ir.created_at::text, ir.updated_at::text, ir.error_message, "
        "u.username as lock_owner_username, u.display_name as lock_owner_display_name, "
        "l.acquired_at::text as lock_acquired_at, l.expires_at::text as lock_expires_at, "
        "sf.status as temporary_source_status, sf.expires_at::text as temporary_source_expires_at "
        "from import_records ir "
        "left join import_source_files sf on sf.import_record_id=ir.id "
        "left join import_record_edit_locks l on l.import_record_id = ir.id and l.expires_at > now() "
        "left join users u on u.id = l.user_id "
        "where ir.inspection_year_id = $1::uuid order by ir.created_at desc", inspection_year_id);

    for (const auto& row : import_rows) {
        review::WorkspaceImport item;
        item.id = row["id"].as<std::string>();
        item.system_number = row["system_number"].as<std::string>();
        item.import_name = row["import_name"].as<std::string>();
        item.source_type = row["source_type"].as<std::string>();
        item.import_status = row["import_status"].as<std::string>();
        item.importer_name = optional_text(row, "importer_name");
        item.created_at = optional_text(row, "created_at");
        item.updated_at = optional_text(row, "updated_at");
        item.error_message = optional_text(row, "error_message");
        item.temporary_source_status = optional_text(row, "temporary_source_status");
        item.temporary_source_expires_at = optional_text(row, "temporary_source_expires_at");
        item.statistics = review::build_review_statistics(
            parse_json_or_empty(row["parsed_result_json"].as<std::string>()));
        const auto owner_username = optional_text(row, "lock_owner_username");
        const auto owner_display_name = optional_text(row, "lock_owner_display_name");
        const auto acquired_at = optional_text(row, "lock_acquired_at");
        const auto expires_at = optional_text(row, "lock_expires_at");
        if (owner_username && owner_display_name && acquired_at && expires_at) {
            item.edit_lock = review::WorkspaceEditLock{*owner_username, *owner_display_name, *acquired_at, *expires_at};
        }
        if (item.import_status == "已上传" || item.import_status == "解析中" || item.import_status == "待校对"
            || item.import_status == "解析失败") {
            ++workspace.pending.import_count;
        }
        workspace.imports.push_back(std::move(item));
    }

    const auto unbound_rows = db_client_->execSqlSync(
        "select count(*)::int as count from defect_observations o "
        "join inspection_years iy on iy.id = o.inspection_year_id "
        "where o.inspection_year_id = $1::uuid and iy.is_current and iy.status = '已确认' "
        "and o.review_status in ('已确认','已修改') and o.defect_thread_id is null", inspection_year_id);
    workspace.pending.unbound_observation_count = unbound_rows[0]["count"].as<int>();
    return workspace;
}

CreateInspectionYearOutcome WorkspaceRepository::create_inspection_year(
    const std::string& bridge_id,
    const int inspection_year,
    const std::string& technical_condition_package_id,
    const std::string& maintenance_package_id,
    const std::string& created_by_user_id
) {
    std::shared_ptr<drogon::orm::Transaction> transaction;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        transaction = db_client_->newTransaction(latch->callback());
        transaction->execSqlSync(
            "select pg_advisory_xact_lock(hashtext($1::text), $2::integer)",
            bridge_id, inspection_year);
        const auto bridge_rows = transaction->execSqlSync(
            "select 1 from bridges where id=$1::uuid for share", bridge_id);
        if (bridge_rows.empty()) {
            transaction->rollback();
            return {CreateInspectionYearStatus::BridgeNotFound, std::nullopt, std::nullopt};
        }
        const auto existing_rows = transaction->execSqlSync(
            "select id::text from inspection_years "
            "where bridge_id=$1::uuid and inspection_year=$2 and is_current limit 1",
            bridge_id, inspection_year);
        if (!existing_rows.empty()) {
            transaction->rollback();
            return {CreateInspectionYearStatus::AlreadyExists, std::nullopt,
                    existing_rows[0]["id"].as<std::string>()};
        }

        const auto packages = transaction->execSqlSync(
            "select id::text as id, standard_family, is_enabled, sync_status "
            "from standard_packages where id in ($1::uuid, $2::uuid)",
            technical_condition_package_id, maintenance_package_id);
        if (packages.size() != 2) {
            transaction->rollback();
            return {CreateInspectionYearStatus::PackageNotFound, std::nullopt, std::nullopt};
        }
        bool technical_ok = false;
        bool maintenance_ok = false;
        bool available = true;
        for (const auto& package : packages) {
            const auto id = package["id"].as<std::string>();
            const auto family = package["standard_family"].as<std::string>();
            technical_ok = technical_ok ||
                (id == technical_condition_package_id && family == "technical_condition");
            maintenance_ok = maintenance_ok ||
                (id == maintenance_package_id && family == "maintenance");
            available = available && package["is_enabled"].as<bool>() &&
                package["sync_status"].as<std::string>() == "正常";
        }
        if (!technical_ok || !maintenance_ok) {
            transaction->rollback();
            return {CreateInspectionYearStatus::FamilyMismatch, std::nullopt, std::nullopt};
        }
        if (!available) {
            transaction->rollback();
            return {CreateInspectionYearStatus::PackageUnavailable, std::nullopt, std::nullopt};
        }

        const auto profile_rows = transaction->execSqlSync(
            "insert into project_standard_profiles "
            "(technical_condition_package_id, maintenance_package_id, created_by_user_id, change_reason) "
            "values ($1::uuid, $2::uuid, $3::uuid, '创建年度检测时锁定规范组合') "
            "returning id::text as id",
            technical_condition_package_id, maintenance_package_id, created_by_user_id);
        const auto inserted_rows = transaction->execSqlSync(
            "insert into inspection_years "
            "(bridge_id, inspection_year, status, version_number, is_current, standard_profile_id) "
            "values ($1::uuid, $2, '待校对', 1, true, $3::uuid) "
            "returning id::text as inspection_id, system_number as inspection_system_number, "
            "inspection_year, status as inspection_status, version_number, is_current, "
            "overall_score, overall_grade, created_at::text as inspection_created_at, "
            "updated_at::text as inspection_updated_at",
            bridge_id, inspection_year, profile_rows[0]["id"].as<std::string>());
        const auto created = inspection_from_row(inserted_rows[0]);
        transaction.reset();
        if (!latch->wait()) {
            return {CreateInspectionYearStatus::Failed, std::nullopt, std::nullopt};
        }
        return {CreateInspectionYearStatus::Created, created, std::nullopt};
    } catch (...) {
        if (transaction) {
            try { transaction->rollback(); } catch (...) {}
        }
        return {CreateInspectionYearStatus::Failed, std::nullopt, std::nullopt};
    }
}

UploadWordOutcome WorkspaceRepository::upload_word_import(
    const std::string& inspection_year_id,
    const std::string& source_type,
    const archive::WordInputMetadata& metadata,
    const std::string_view content,
    const std::filesystem::path& temporary_word_root
) {
    std::shared_ptr<drogon::orm::Transaction> transaction;
    auto latch = std::make_shared<CommitLatch>();
    std::filesystem::path stored_relative_path;
    bool file_stored = false;
    try {
        transaction = db_client_->newTransaction(latch->callback());
        const auto context_rows = transaction->execSqlSync(
            "select iy.inspection_year, iy.is_current, b.id::text as bridge_id, "
            "b.system_number as bridge_system_number, b.bridge_name "
            "from inspection_years iy join bridges b on b.id = iy.bridge_id "
            "where iy.id = $1::uuid for update", inspection_year_id);
        if (context_rows.empty()) {
            transaction->rollback();
            return {UploadWordStatus::InspectionYearNotFound, std::nullopt};
        }
        const auto& context = context_rows[0];
        if (!context["is_current"].as<bool>()) {
            transaction->rollback();
            return {UploadWordStatus::InspectionYearNotCurrent, std::nullopt};
        }

        const auto import_rows = transaction->execSqlSync(
            "insert into import_records (bridge_id, inspection_year_id, import_name, source_type, import_status) "
            "values ($1::uuid, $2::uuid, $3, $4, '已上传') "
            "returning id::text, system_number, created_at::text, updated_at::text",
            context["bridge_id"].as<std::string>(), inspection_year_id,
            metadata.original_file_name, source_type);
        const auto& import_row = import_rows[0];
        const auto import_id = import_row["id"].as<std::string>();
        const auto import_number = import_row["system_number"].as<std::string>();

        const auto source_rows = transaction->execSqlSync(
            "with source_id as (select gen_random_uuid() as id) "
            "insert into import_source_files "
            "(id, import_record_id, original_file_name, storage_relative_path, file_extension, "
            " file_size_bytes, file_hash, status) "
            "select id, $1::uuid, $2, id::text || '.docx', $3, $4, $5, '待解析' from source_id "
            "returning id::text, system_number, storage_relative_path",
            import_id, metadata.original_file_name, metadata.file_extension,
            static_cast<long long>(metadata.file_size_bytes), metadata.sha256);
        stored_relative_path = std::filesystem::path(
            source_rows[0]["storage_relative_path"].as<std::string>());

        archive::store_temporary_word(
            temporary_word_root, stored_relative_path, content, metadata.sha256);
        file_stored = true;

        transaction.reset();
        if (!latch->wait()) {
            try { archive::remove_temporary_word(temporary_word_root, stored_relative_path); }
            catch (...) {
            }
            return {UploadWordStatus::TemporaryStorageFailed, std::nullopt};
        }

        review::WorkspaceImport imported;
        imported.id = import_id;
        imported.system_number = import_number;
        imported.import_name = metadata.original_file_name;
        imported.source_type = source_type;
        imported.import_status = "已上传";
        imported.created_at = optional_text(import_row, "created_at");
        imported.updated_at = optional_text(import_row, "updated_at");
        return {UploadWordStatus::Created, std::move(imported)};
    } catch (const std::exception& error) {
        if (transaction) {
            try { transaction->rollback(); }
            catch (...) {
            }
        }
        if (file_stored) {
            try { archive::remove_temporary_word(temporary_word_root, stored_relative_path); }
            catch (...) {
            }
        }
        LOG_ERROR << "Temporary Word source transaction failed: " << error.what();
        return {UploadWordStatus::TemporaryStorageFailed, std::nullopt};
    } catch (...) {
        if (transaction) {
            try { transaction->rollback(); }
            catch (...) {
            }
        }
        if (file_stored) {
            try { archive::remove_temporary_word(temporary_word_root, stored_relative_path); }
            catch (...) {
            }
        }
        LOG_ERROR << "Temporary Word source transaction failed with an unknown exception";
        return {UploadWordStatus::TemporaryStorageFailed, std::nullopt};
    }
}

}  // namespace bridge_report::db
