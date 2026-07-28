#include "bridge_report/db/RatingTreeRepository.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <sstream>
#include <utility>

#include <json/json.h>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::optional<std::string> optional_text(
    const drogon::orm::Row& row,
    const char* column) {
    if (row[column].isNull()) {
        return std::nullopt;
    }
    return row[column].as<std::string>();
}

RatingTreeVersionRecord row_to_version(const drogon::orm::Row& row) {
    RatingTreeVersionRecord version;
    version.id = row["id"].as<std::string>();
    version.tree_code = row["tree_code"].as<std::string>();
    version.tree_name = row["tree_name"].as<std::string>();
    version.package_version = row["package_version"].as<std::string>();
    version.tree_content_checksum = row["tree_content_checksum"].as<std::string>();
    version.technical_condition_package_id =
        row["technical_condition_package_id"].as<std::string>();
    version.maintenance_package_id =
        row["maintenance_package_id"].as<std::string>();
    version.status = row["status"].as<std::string>();
    version.published_at = optional_text(row, "published_at");
    return version;
}

const char* version_columns() {
    return "id::text as id, tree_code, tree_name, package_version, "
           "tree_content_checksum, "
           "technical_condition_package_id::text as technical_condition_package_id, "
           "maintenance_package_id::text as maintenance_package_id, status, "
           "published_at::text as published_at";
}

std::string join(const std::vector<std::string>& values, const char separator) {
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) result.push_back(separator);
        result += values[index];
    }
    return result;
}

std::string join_scales(const std::vector<int>& values) {
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) result.push_back(',');
        result += std::to_string(values[index]);
    }
    return result;
}

std::string normalize_alias(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isspace(ch) || ch == ',' || ch == '.' || ch == '-' ||
            ch == '_' || ch == '/' || ch == '(' || ch == ')') {
            continue;
        }
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

std::string node_detail_json(const rating_tree::EffectiveRatingTreeNode& node) {
    Json::Value detail(Json::objectValue);
    detail["h21_indicator_name"] = node.h21_indicator_name;
    detail["h21_source_table"] = node.h21_source_table;
    detail["scale_descriptions"] = Json::Value(Json::objectValue);
    detail["deduction_points"] = Json::Value(Json::objectValue);
    detail["source_ids"] = Json::Value(Json::arrayValue);
    for (const auto& source_id : node.source_ids) {
        detail["source_ids"].append(source_id);
    }
    for (const auto& [scale, description] : node.scale_descriptions) {
        detail["scale_descriptions"][std::to_string(scale)] = description;
    }
    for (const auto& [scale, points] : node.deduction_points) {
        detail["deduction_points"][std::to_string(scale)] = points;
    }
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, detail);
}

std::optional<std::string> find_source_package_id(
    const drogon::orm::DbClientPtr& client,
    const std::string& family,
    const std::string& standard_id,
    const std::string& package_version,
    const std::string& checksum) {
    const auto rows = client->execSqlSync(
        "select id::text as id from standard_packages "
        "where standard_family=$1 and standard_id=$2 and package_version=$3 "
        "and content_checksum=$4 and is_enabled and sync_status='正常'",
        family,
        standard_id,
        package_version,
        checksum);
    if (rows.size() != 1) {
        return std::nullopt;
    }
    return rows[0]["id"].as<std::string>();
}

}  // namespace

RatingTreeRepository::RatingTreeRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

RatingTreeSyncOutcome RatingTreeRepository::sync_published_tree(
    const rating_tree::EffectiveRatingTree& tree,
    const int contract_version) {
    if (!tree.version.maintenance_standard_id.has_value() ||
        !tree.version.maintenance_package_version.has_value() ||
        !tree.version.maintenance_content_checksum.has_value()) {
        return {RatingTreeSyncStatus::SourcePackageNotFound, std::nullopt};
    }
    const auto technical_id = find_source_package_id(
        db_client_,
        "technical_condition",
        tree.version.h21_standard_id,
        tree.version.h21_package_version,
        tree.version.h21_content_checksum);
    const auto maintenance_id = find_source_package_id(
        db_client_,
        "maintenance",
        *tree.version.maintenance_standard_id,
        *tree.version.maintenance_package_version,
        *tree.version.maintenance_content_checksum);
    if (!technical_id.has_value() || !maintenance_id.has_value()) {
        return {RatingTreeSyncStatus::SourcePackageNotFound, std::nullopt};
    }

    const auto existing = db_client_->execSqlSync(
        "select id::text as id, tree_content_checksum, status "
        "from rating_tree_versions where tree_code=$1 and package_version=$2",
        tree.version.tree_code,
        tree.version.package_version);
    if (!existing.empty()) {
        const auto id = existing[0]["id"].as<std::string>();
        if (existing[0]["tree_content_checksum"].as<std::string>() ==
                tree.version.tree_content_checksum &&
            existing[0]["status"].as<std::string>() == "published") {
            return {RatingTreeSyncStatus::Unchanged, id};
        }
        return {RatingTreeSyncStatus::ChecksumConflict, id};
    }

    TransactionPtr transaction;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&]() {
        if (transaction) {
            try {
                transaction->rollback();
            } catch (...) {
            }
        }
    };
    try {
        transaction = db_client_->newTransaction(latch->callback());
        const auto inserted = transaction->execSqlSync(
            "insert into rating_tree_versions ("
            "tree_code, tree_name, package_version, contract_version, "
            "technical_condition_package_id, technical_condition_standard_id, "
            "technical_condition_package_version, technical_condition_content_checksum, "
            "maintenance_package_id, maintenance_standard_id, "
            "maintenance_package_version, maintenance_content_checksum, "
            "organization_tree_code, organization_package_version, "
            "organization_content_checksum, tree_content_checksum, status"
            ") values ("
            "$1, $2, $3, $4, $5::uuid, $6, $7, $8, $9::uuid, $10, $11, $12, "
            "$13, $14, $15, $16, 'draft'"
            ") returning id::text as id",
            tree.version.tree_code,
            tree.version.tree_name,
            tree.version.package_version,
            contract_version,
            *technical_id,
            tree.version.h21_standard_id,
            tree.version.h21_package_version,
            tree.version.h21_content_checksum,
            *maintenance_id,
            *tree.version.maintenance_standard_id,
            *tree.version.maintenance_package_version,
            *tree.version.maintenance_content_checksum,
            tree.version.tree_code,
            tree.version.package_version,
            tree.version.organization_content_checksum,
            tree.version.tree_content_checksum);
        const auto version_id = inserted[0]["id"].as<std::string>();

        std::map<std::string, std::string> node_ids;
        for (const auto& [node_key, node] : tree.nodes) {
            const auto rows = transaction->execSqlSync(
                "insert into rating_tree_nodes ("
                "rating_tree_version_id, node_key, display_name, node_type, sort_order, "
                "bridge_type_ids, component_category_ids, scoring_mode, h21_indicator_id, "
                "is_selectable, is_scoring, organization_note, allowed_scales, detail_json"
                ") values ("
                "$1::uuid, $2, $3, $4, $5, "
                "case when $6='' then '{}'::text[] else string_to_array($6, chr(31)) end, "
                "case when $7='' then '{}'::text[] else string_to_array($7, chr(31)) end, "
                "$8, nullif($9, ''), $10, $11, $12, "
                "case when $13='' then '{}'::integer[] "
                "else string_to_array($13, ',')::integer[] end, $14::jsonb"
                ") returning id::text as id",
                version_id,
                node_key,
                node.display_name,
                rating_tree::to_string(node.node_type),
                node.sort_order,
                join(node.bridge_type_ids, '\x1f'),
                join(node.component_category_ids, '\x1f'),
                rating_tree::to_string(node.scoring_mode),
                node.h21_indicator_id.value_or(""),
                node.is_selectable,
                node.is_scoring,
                node.organization_note,
                join_scales(node.allowed_scales),
                node_detail_json(node));
            node_ids.emplace(node_key, rows[0]["id"].as<std::string>());
        }

        for (const auto& [node_key, node] : tree.nodes) {
            if (node.parent_id.has_value()) {
                transaction->execSqlSync(
                    "update rating_tree_nodes set parent_node_id=$3::uuid "
                    "where rating_tree_version_id=$1::uuid and id=$2::uuid",
                    version_id,
                    node_ids.at(node_key),
                    node_ids.at(*node.parent_id));
            }
            for (const auto& source_key : node.source_ids) {
                const auto source = tree.sources.find(source_key);
                if (source == tree.sources.end()) {
                    throw std::runtime_error("rating tree node source is missing");
                }
                transaction->execSqlSync(
                    "insert into rating_tree_node_sources ("
                    "rating_tree_node_id, source_key, source_type, title, source_reference"
                    ") values ($1::uuid, $2, $3, $4, $5)",
                    node_ids.at(node_key),
                    source->second.id,
                    source->second.source_type,
                    source->second.title,
                    source->second.reference);
            }
        }

        for (const auto& alias : tree.aliases) {
            transaction->execSqlSync(
                "insert into rating_tree_aliases ("
                "rating_tree_version_id, target_node_id, bridge_type_id, "
                "component_category_id, alias_text, normalized_alias"
                ") values ($1::uuid, $2::uuid, $3, $4, $5, $6)",
                version_id,
                node_ids.at(alias.target_node_id),
                alias.bridge_type_id,
                alias.component_category_id,
                alias.alias,
                normalize_alias(alias.alias));
        }

        transaction->execSqlSync(
            "update rating_tree_versions "
            "set status='published', published_at=now(), updated_at=now() "
            "where id=$1::uuid",
            version_id);
        transaction.reset();
        if (!latch->wait()) {
            return {RatingTreeSyncStatus::Failed, std::nullopt};
        }
        return {RatingTreeSyncStatus::Inserted, version_id};
    } catch (const drogon::orm::DrogonDbException&) {
        rollback();
        const auto conflict = db_client_->execSqlSync(
            "select id::text as id, tree_content_checksum, status "
            "from rating_tree_versions where tree_code=$1 and package_version=$2",
            tree.version.tree_code,
            tree.version.package_version);
        if (!conflict.empty()) {
            const auto id = conflict[0]["id"].as<std::string>();
            if (conflict[0]["tree_content_checksum"].as<std::string>() ==
                    tree.version.tree_content_checksum &&
                conflict[0]["status"].as<std::string>() == "published") {
                return {RatingTreeSyncStatus::Unchanged, id};
            }
            return {RatingTreeSyncStatus::ChecksumConflict, id};
        }
        return {RatingTreeSyncStatus::Failed, std::nullopt};
    } catch (...) {
        rollback();
        return {RatingTreeSyncStatus::Failed, std::nullopt};
    }
}

std::optional<RatingTreeVersionRecord> RatingTreeRepository::find_version_by_id(
    const std::string& version_id) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + version_columns() +
            " from rating_tree_versions where id=$1::uuid",
        version_id);
    return rows.empty()
        ? std::nullopt
        : std::optional<RatingTreeVersionRecord>(row_to_version(rows[0]));
}

std::optional<RatingTreeVersionRecord> RatingTreeRepository::find_published_version(
    const std::string& tree_code,
    const std::string& package_version) const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + version_columns() +
            " from rating_tree_versions "
            "where tree_code=$1 and package_version=$2 and status='published'",
        tree_code,
        package_version);
    return rows.empty()
        ? std::nullopt
        : std::optional<RatingTreeVersionRecord>(row_to_version(rows[0]));
}

std::vector<RatingTreeVersionRecord>
RatingTreeRepository::list_published_versions() const {
    const auto rows = db_client_->execSqlSync(
        std::string("select ") + version_columns() +
        " from rating_tree_versions where status='published' "
        "order by published_at desc, tree_code, package_version");
    std::vector<RatingTreeVersionRecord> versions;
    versions.reserve(rows.size());
    for (const auto& row : rows) {
        versions.push_back(row_to_version(row));
    }
    return versions;
}

RatingTreeProfileBackfillOutcome
RatingTreeRepository::backfill_unique_profile_versions() {
    RatingTreeProfileBackfillOutcome outcome;
    const auto ambiguous = db_client_->execSqlSync(
        "select count(*)::bigint as count from project_standard_profiles p "
        "where p.rating_tree_version_id is null and ("
        "select count(*) from rating_tree_versions v "
        "where v.status='published' "
        "and v.technical_condition_package_id=p.technical_condition_package_id "
        "and v.maintenance_package_id=p.maintenance_package_id"
        ") > 1");
    outcome.ambiguous_profile_count =
        static_cast<std::size_t>(ambiguous[0]["count"].as<long long>());

    const auto updated = db_client_->execSqlSync(
        "with unique_tree as ("
        "select p.id as profile_id, min(v.id) as tree_id "
        "from project_standard_profiles p "
        "join rating_tree_versions v "
        "on v.technical_condition_package_id=p.technical_condition_package_id "
        "and v.maintenance_package_id=p.maintenance_package_id "
        "and v.status='published' "
        "where p.rating_tree_version_id is null "
        "group by p.id having count(*)=1"
        ") update project_standard_profiles p "
        "set rating_tree_version_id=u.tree_id, updated_at=now() "
        "from unique_tree u where p.id=u.profile_id returning p.id");
    outcome.updated_count = updated.size();
    return outcome;
}

}  // namespace bridge_report::db
