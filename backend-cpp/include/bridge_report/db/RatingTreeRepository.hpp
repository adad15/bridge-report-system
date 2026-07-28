#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/rating_tree/RatingTreeModels.hpp"

namespace bridge_report::db {

enum class RatingTreeSyncStatus {
    Inserted,
    Unchanged,
    ChecksumConflict,
    SourcePackageNotFound,
    Failed,
};

struct RatingTreeSyncOutcome {
    RatingTreeSyncStatus status{RatingTreeSyncStatus::Failed};
    std::optional<std::string> rating_tree_version_id;
};

struct RatingTreeVersionRecord {
    std::string id;
    std::string tree_code;
    std::string tree_name;
    std::string package_version;
    std::string tree_content_checksum;
    std::string technical_condition_package_id;
    std::string maintenance_package_id;
    std::string status;
    std::optional<std::string> published_at;
};

struct RatingTreeProfileBackfillOutcome {
    std::size_t updated_count{0};
    std::size_t ambiguous_profile_count{0};
};

class RatingTreeRepository {
public:
    explicit RatingTreeRepository(drogon::orm::DbClientPtr db_client);

    RatingTreeSyncOutcome sync_published_tree(
        const rating_tree::EffectiveRatingTree& tree,
        int contract_version = 1);

    std::optional<RatingTreeVersionRecord> find_version_by_id(
        const std::string& version_id) const;
    std::optional<RatingTreeVersionRecord> find_published_version(
        const std::string& tree_code,
        const std::string& package_version) const;
    std::vector<RatingTreeVersionRecord> list_published_versions() const;

    RatingTreeProfileBackfillOutcome backfill_unique_profile_versions();

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
