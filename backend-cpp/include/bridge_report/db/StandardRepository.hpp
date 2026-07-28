#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/standards/StandardModels.hpp"

namespace bridge_report::db {

enum class StandardPackageSyncStatus {
    Inserted,
    Unchanged,
    ChecksumConflict,
};

struct StandardPackageSyncOutcome {
    StandardPackageSyncStatus status{StandardPackageSyncStatus::ChecksumConflict};
    std::optional<std::string> package_id;
};

struct StandardPackageRecord {
    std::string id;
    standards::StandardFamily family{standards::StandardFamily::technical_condition};
    std::string standard_id;
    std::string standard_code;
    std::string standard_name;
    std::string official_edition;
    std::string package_version;
    int contract_version{0};
    std::string algorithm_id;
    std::string effective_date;
    std::string content_checksum;
    bool is_enabled{false};
    std::string sync_status;
    std::optional<std::string> sync_error_code;
    std::optional<std::string> sync_error_message;
};

enum class SetStandardPackageEnabledStatus {
    Updated,
    NotFound,
    Forbidden,
    FaultBlocked,
};

struct CreateStandardProfileRequest {
    std::string rating_tree_version_id;
    std::string created_by_user_id;
    std::string change_reason;
};

struct ProjectStandardProfileRecord {
    std::string id;
    std::string profile_series_id;
    int revision_number{0};
    std::string technical_condition_package_id;
    std::string maintenance_package_id;
    std::string rating_tree_version_id;
    std::optional<std::string> supersedes_profile_id;
    std::string status;
    std::string change_reason;
};

enum class CreateStandardProfileStatus {
    Created,
    SourceProfileNotFound,
    PackageNotFound,
    PackageUnavailable,
    FamilyMismatch,
    RatingTreeNotFound,
    RatingTreeUnavailable,
};

struct CreateStandardProfileOutcome {
    CreateStandardProfileStatus status{CreateStandardProfileStatus::PackageNotFound};
    std::optional<ProjectStandardProfileRecord> profile;
};

class StandardRepository {
public:
    explicit StandardRepository(drogon::orm::DbClientPtr db_client);

    StandardPackageSyncOutcome sync_package(const standards::StandardManifest& manifest);
    std::vector<StandardPackageSyncOutcome> sync_packages(
        const std::vector<standards::StandardManifest>& manifests);

    std::optional<StandardPackageRecord> find_package(
        standards::StandardFamily family,
        const std::string& standard_id,
        const std::string& package_version);
    std::optional<StandardPackageRecord> find_package_by_id(const std::string& package_id);
    std::vector<StandardPackageRecord> list_packages(bool enabled_only);

    SetStandardPackageEnabledStatus set_package_enabled(
        const std::string& package_id,
        bool enabled,
        const std::string& actor_role);
    bool mark_package_fault(
        const std::string& package_id,
        const std::string& error_code,
        const std::string& error_message);

    CreateStandardProfileOutcome create_profile(const CreateStandardProfileRequest& request);
    CreateStandardProfileOutcome revise_profile(
        const std::string& source_profile_id,
        const CreateStandardProfileRequest& request);
    bool inherit_profile_for_revision(
        const std::string& source_inspection_year_id,
        const std::string& revision_inspection_year_id);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
