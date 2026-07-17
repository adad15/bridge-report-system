#include "bridge_report/db/StandardRepository.hpp"

#include <utility>

namespace bridge_report::db {
namespace {

std::optional<std::string> optional_text(
    const drogon::orm::Row& row,
    const char* column) {
    if (row[column].isNull()) {
        return std::nullopt;
    }
    return row[column].as<std::string>();
}

StandardPackageRecord row_to_package(const drogon::orm::Row& row) {
    StandardPackageRecord package;
    package.id = row["id"].as<std::string>();
    package.family = *standards::parse_standard_family(row["standard_family"].as<std::string>());
    package.standard_id = row["standard_id"].as<std::string>();
    package.standard_code = row["standard_code"].as<std::string>();
    package.standard_name = row["standard_name"].as<std::string>();
    package.official_edition = row["official_edition"].as<std::string>();
    package.package_version = row["package_version"].as<std::string>();
    package.contract_version = row["contract_version"].as<int>();
    package.algorithm_id = row["algorithm_id"].as<std::string>();
    package.effective_date = row["effective_date"].as<std::string>();
    package.content_checksum = row["content_checksum"].as<std::string>();
    package.is_enabled = row["is_enabled"].as<bool>();
    package.sync_status = row["sync_status"].as<std::string>();
    package.sync_error_code = optional_text(row, "sync_error_code");
    package.sync_error_message = optional_text(row, "sync_error_message");
    return package;
}

ProjectStandardProfileRecord row_to_profile(const drogon::orm::Row& row) {
    ProjectStandardProfileRecord profile;
    profile.id = row["id"].as<std::string>();
    profile.profile_series_id = row["profile_series_id"].as<std::string>();
    profile.revision_number = row["revision_number"].as<int>();
    profile.technical_condition_package_id =
        row["technical_condition_package_id"].as<std::string>();
    profile.maintenance_package_id = row["maintenance_package_id"].as<std::string>();
    profile.supersedes_profile_id = optional_text(row, "supersedes_profile_id");
    profile.status = row["status"].as<std::string>();
    profile.change_reason = row["change_reason"].as<std::string>();
    return profile;
}

const char* package_columns() {
    return "id::text as id, standard_family, standard_id, standard_code, standard_name, "
           "official_edition, package_version, contract_version, algorithm_id, "
           "effective_date::text as effective_date, content_checksum, is_enabled, sync_status, "
           "sync_error_code, sync_error_message";
}

const char* profile_returning_columns() {
    return "id::text as id, profile_series_id::text as profile_series_id, revision_number, "
           "technical_condition_package_id::text as technical_condition_package_id, "
           "maintenance_package_id::text as maintenance_package_id, "
           "supersedes_profile_id::text as supersedes_profile_id, status, change_reason";
}

CreateStandardProfileStatus validate_profile_packages(
    const drogon::orm::DbClientPtr& db_client,
    const CreateStandardProfileRequest& request) {
    const auto packages = db_client->execSqlSync(
        "select id::text as id, standard_family, is_enabled, sync_status "
        "from standard_packages where id in ($1::uuid, $2::uuid)",
        request.technical_condition_package_id,
        request.maintenance_package_id);
    if (packages.size() != 2) {
        return CreateStandardProfileStatus::PackageNotFound;
    }

    bool found_technical = false;
    bool found_maintenance = false;
    bool available = true;
    for (const auto& package : packages) {
        const auto id = package["id"].as<std::string>();
        const auto family = package["standard_family"].as<std::string>();
        if (id == request.technical_condition_package_id) {
            found_technical = family == "technical_condition";
        }
        if (id == request.maintenance_package_id) {
            found_maintenance = family == "maintenance";
        }
        available = available && package["is_enabled"].as<bool>() &&
                    package["sync_status"].as<std::string>() == "正常";
    }
    if (!found_technical || !found_maintenance) {
        return CreateStandardProfileStatus::FamilyMismatch;
    }
    if (!available) {
        return CreateStandardProfileStatus::PackageUnavailable;
    }
    return CreateStandardProfileStatus::Created;
}

}  // namespace

StandardRepository::StandardRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

StandardPackageSyncOutcome StandardRepository::sync_package(
    const standards::StandardManifest& manifest) {
    const auto result = db_client_->execSqlSync(
        "insert into standard_packages ("
        "standard_family, standard_id, standard_code, standard_name, official_edition, "
        "package_version, contract_version, algorithm_id, effective_date, content_checksum"
        ") values ($1, $2, $3, $4, $5, $6, $7, $8, $9::date, $10) "
        "on conflict (standard_family, standard_id, package_version) do update set "
        "synchronized_at=now(), updated_at=now(), sync_status='正常', "
        "sync_error_code=null, sync_error_message=null "
        "where standard_packages.content_checksum=excluded.content_checksum "
        "returning id::text as id, (xmax=0) as inserted",
        standards::to_string(manifest.family),
        manifest.standard_id,
        manifest.standard_code,
        manifest.standard_name,
        manifest.official_edition,
        manifest.package_version,
        manifest.contract_version,
        manifest.algorithm_id,
        manifest.effective_date,
        manifest.content_checksum);

    if (result.empty()) {
        const auto existing = db_client_->execSqlSync(
            "select id::text as id from standard_packages "
            "where standard_family=$1 and standard_id=$2 and package_version=$3",
            standards::to_string(manifest.family),
            manifest.standard_id,
            manifest.package_version);
        return {
            StandardPackageSyncStatus::ChecksumConflict,
            existing.empty() ? std::nullopt
                             : std::optional<std::string>(existing[0]["id"].as<std::string>()),
        };
    }

    return {
        result[0]["inserted"].as<bool>() ? StandardPackageSyncStatus::Inserted
                                         : StandardPackageSyncStatus::Unchanged,
        result[0]["id"].as<std::string>(),
    };
}

std::vector<StandardPackageSyncOutcome> StandardRepository::sync_packages(
    const std::vector<standards::StandardManifest>& manifests) {
    std::vector<StandardPackageSyncOutcome> outcomes;
    outcomes.reserve(manifests.size());
    for (const auto& manifest : manifests) {
        outcomes.push_back(sync_package(manifest));
    }
    return outcomes;
}

std::optional<StandardPackageRecord> StandardRepository::find_package(
    standards::StandardFamily family,
    const std::string& standard_id,
    const std::string& package_version) {
    const auto result = db_client_->execSqlSync(
        std::string("select ") + package_columns() +
            " from standard_packages where standard_family=$1 and standard_id=$2 "
            "and package_version=$3",
        standards::to_string(family),
        standard_id,
        package_version);
    if (result.empty()) {
        return std::nullopt;
    }
    return row_to_package(result[0]);
}

SetStandardPackageEnabledStatus StandardRepository::set_package_enabled(
    const std::string& package_id,
    const bool enabled,
    const std::string& actor_role) {
    if (actor_role != "admin") {
        return SetStandardPackageEnabledStatus::Forbidden;
    }
    const auto existing = db_client_->execSqlSync(
        "select sync_status from standard_packages where id=$1::uuid",
        package_id);
    if (existing.empty()) {
        return SetStandardPackageEnabledStatus::NotFound;
    }
    if (enabled && existing[0]["sync_status"].as<std::string>() == "故障") {
        return SetStandardPackageEnabledStatus::FaultBlocked;
    }
    db_client_->execSqlSync(
        "update standard_packages set is_enabled=$2, updated_at=now() where id=$1::uuid",
        package_id,
        enabled);
    return SetStandardPackageEnabledStatus::Updated;
}

bool StandardRepository::mark_package_fault(
    const std::string& package_id,
    const std::string& error_code,
    const std::string& error_message) {
    const auto result = db_client_->execSqlSync(
        "update standard_packages set sync_status='故障', is_enabled=false, "
        "sync_error_code=$2, sync_error_message=$3, synchronized_at=now(), updated_at=now() "
        "where id=$1::uuid returning id",
        package_id,
        error_code,
        error_message);
    return !result.empty();
}

CreateStandardProfileOutcome StandardRepository::create_profile(
    const CreateStandardProfileRequest& request) {
    const auto validation = validate_profile_packages(db_client_, request);
    if (validation != CreateStandardProfileStatus::Created) {
        return {validation, std::nullopt};
    }
    const auto result = db_client_->execSqlSync(
        std::string("insert into project_standard_profiles (") +
            "technical_condition_package_id, maintenance_package_id, created_by_user_id, "
            "change_reason) values ($1::uuid, $2::uuid, $3::uuid, $4) returning " +
            profile_returning_columns(),
        request.technical_condition_package_id,
        request.maintenance_package_id,
        request.created_by_user_id,
        request.change_reason);
    return {CreateStandardProfileStatus::Created, row_to_profile(result[0])};
}

CreateStandardProfileOutcome StandardRepository::revise_profile(
    const std::string& source_profile_id,
    const CreateStandardProfileRequest& request) {
    const auto validation = validate_profile_packages(db_client_, request);
    if (validation != CreateStandardProfileStatus::Created) {
        return {validation, std::nullopt};
    }
    const auto source = db_client_->execSqlSync(
        "select profile_series_id::text as profile_series_id "
        "from project_standard_profiles where id=$1::uuid",
        source_profile_id);
    if (source.empty()) {
        return {CreateStandardProfileStatus::SourceProfileNotFound, std::nullopt};
    }

    const auto result = db_client_->execSqlSync(
        std::string("insert into project_standard_profiles (") +
            "profile_series_id, revision_number, technical_condition_package_id, "
            "maintenance_package_id, supersedes_profile_id, created_by_user_id, change_reason) "
            "select $2::uuid, coalesce(max(revision_number), 0) + 1, $3::uuid, $4::uuid, "
            "$1::uuid, $5::uuid, $6 from project_standard_profiles "
            "where profile_series_id=$2::uuid returning " + profile_returning_columns(),
        source_profile_id,
        source[0]["profile_series_id"].as<std::string>(),
        request.technical_condition_package_id,
        request.maintenance_package_id,
        request.created_by_user_id,
        request.change_reason);
    return {CreateStandardProfileStatus::Created, row_to_profile(result[0])};
}

bool StandardRepository::inherit_profile_for_revision(
    const std::string& source_inspection_year_id,
    const std::string& revision_inspection_year_id) {
    const auto result = db_client_->execSqlSync(
        "update inspection_years revision set standard_profile_id=source.standard_profile_id, "
        "updated_at=now() from inspection_years source "
        "where source.id=$1::uuid and revision.id=$2::uuid "
        "and revision.revision_source_inspection_id=source.id "
        "and source.standard_profile_id is not null and revision.status='待校对' "
        "returning revision.id",
        source_inspection_year_id,
        revision_inspection_year_id);
    return !result.empty();
}

}  // namespace bridge_report::db
