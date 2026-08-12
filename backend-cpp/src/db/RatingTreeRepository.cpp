#include "bridge_report/db/RatingTreeRepository.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <sstream>
#include <utility>

#include <json/json.h>

#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/rating_tree/RatingTreeMatchText.hpp"

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

// 别名唯一性键与匹配器共用同一套规范化：删掉文字内部的 `/`、`-` 会让
// "板底/腹板交界处" 之类的合法写法失去区分度。
std::string normalize_alias(const std::string& value) {
    return rating_tree::normalize_match_key(value);
}

std::string node_detail_json(const rating_tree::EffectiveRatingTreeNode& node) {
    Json::Value detail(Json::objectValue);
    detail["h21_indicator_name"] = node.h21_indicator_name;
    detail["h21_source_table"] = node.h21_source_table;
    detail["uses_source_scale_descriptions"] =
        node.uses_source_scale_descriptions;
    detail["scale_descriptions"] = Json::Value(Json::objectValue);
    detail["deduction_points"] = Json::Value(Json::objectValue);
    detail["source_ids"] = Json::Value(Json::arrayValue);
    detail["source_mappings"] = Json::Value(Json::arrayValue);
    for (const auto& source_id : node.source_ids) {
        detail["source_ids"].append(source_id);
    }
    for (const auto& mapping : node.source_mappings) {
        Json::Value value;
        value["source_group_id"] = mapping.source_group_id;
        value["source_indicator_id"] = mapping.source_indicator_id;
        value["source_group_number"] = mapping.source_group_number;
        value["source_indicator_number"] = mapping.source_indicator_number;
        detail["source_mappings"].append(value);
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
                "rating_tree_version_id, node_key, display_number, display_name, node_type, sort_order, "
                "bridge_type_ids, component_category_ids, scoring_mode, h21_indicator_id, "
                "is_selectable, is_scoring, organization_note, allowed_scales, detail_json"
                ") values ("
                "$1::uuid, $2, nullif($3, ''), $4, $5, $6, "
                "case when $7='' then '{}'::text[] else string_to_array($7, chr(31)) end, "
                "case when $8='' then '{}'::text[] else string_to_array($8, chr(31)) end, "
                "$9, nullif($10, ''), $11, $12, $13, "
                "case when $14='' then '{}'::integer[] "
                "else string_to_array($14, ',')::integer[] end, $15::jsonb"
                ") returning id::text as id",
                version_id,
                node_key,
                node.display_number.value_or(""),
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

        for (const auto& rule : tree.keyword_rules) {
            transaction->execSqlSync(
                "insert into rating_tree_keyword_rules ("
                "rating_tree_version_id, target_node_id, rule_key, bridge_type_id, "
                "component_category_id, positive_keywords, excluded_keywords, "
                "auto_bind, sort_order, rule_note"
                ") values ($1::uuid, $2::uuid, $3, $4, $5, "
                "string_to_array($6, chr(31)), "
                "case when $7='' then '{}'::text[] "
                "else string_to_array($7, chr(31)) end, $8, $9, $10)",
                version_id,
                node_ids.at(rule.target_node_id),
                rule.rule_id,
                rule.bridge_type_id,
                rule.component_category_id,
                join(rule.positive_keywords, '\x1f'),
                join(rule.excluded_keywords, '\x1f'),
                rule.auto_bind,
                rule.sort_order,
                rule.rule_note);
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

std::optional<rating_tree::EffectiveRatingTree>
RatingTreeRepository::load_published_tree(const std::string& version_id) const {
    const auto versions = db_client_->execSqlSync(
        "select tree_code,tree_name,package_version,"
        "technical_condition_standard_id,technical_condition_package_version,"
        "technical_condition_content_checksum,maintenance_standard_id,"
        "maintenance_package_version,maintenance_content_checksum,"
        "organization_content_checksum,tree_content_checksum "
        "from rating_tree_versions where id=$1::uuid and status='published'",
        version_id);
    if (versions.empty()) return std::nullopt;

    rating_tree::EffectiveRatingTree tree;
    const auto& version = versions[0];
    tree.version.tree_code = version["tree_code"].as<std::string>();
    tree.version.tree_name = version["tree_name"].as<std::string>();
    tree.version.package_version = version["package_version"].as<std::string>();
    tree.version.h21_standard_id =
        version["technical_condition_standard_id"].as<std::string>();
    tree.version.h21_package_version =
        version["technical_condition_package_version"].as<std::string>();
    tree.version.h21_content_checksum =
        version["technical_condition_content_checksum"].as<std::string>();
    if (!version["maintenance_standard_id"].isNull()) {
        tree.version.maintenance_standard_id =
            version["maintenance_standard_id"].as<std::string>();
        tree.version.maintenance_package_version =
            version["maintenance_package_version"].as<std::string>();
        tree.version.maintenance_content_checksum =
            version["maintenance_content_checksum"].as<std::string>();
    }
    tree.version.organization_content_checksum =
        version["organization_content_checksum"].as<std::string>();
    tree.version.tree_content_checksum =
        version["tree_content_checksum"].as<std::string>();

    const auto nodes = db_client_->execSqlSync(
        "select id::text as id,parent_node_id::text as parent_node_id,display_number,display_name,"
        "node_type,sort_order,array_to_json(bridge_type_ids)::text as bridge_type_ids,"
        "array_to_json(component_category_ids)::text as component_category_ids,"
        "scoring_mode,h21_indicator_id,is_selectable,is_scoring,organization_note,"
        "array_to_json(allowed_scales)::text as allowed_scales,detail_json::text as detail_json "
        "from rating_tree_nodes where rating_tree_version_id=$1::uuid",
        version_id);
    Json::CharReaderBuilder reader;
    for (const auto& row : nodes) {
        rating_tree::EffectiveRatingTreeNode node;
        node.id = row["id"].as<std::string>();
        if (!row["parent_node_id"].isNull()) {
            node.parent_id = row["parent_node_id"].as<std::string>();
        }
        if (!row["display_number"].isNull()) {
            node.display_number = row["display_number"].as<std::string>();
        }
        node.display_name = row["display_name"].as<std::string>();
        node.node_type = rating_tree::parse_rating_tree_node_type(
            row["node_type"].as<std::string>()).value();
        node.sort_order = row["sort_order"].as<int>();
        node.scoring_mode = rating_tree::parse_rating_tree_scoring_mode(
            row["scoring_mode"].as<std::string>()).value();
        if (!row["h21_indicator_id"].isNull()) {
            node.h21_indicator_id = row["h21_indicator_id"].as<std::string>();
        }
        node.is_selectable = row["is_selectable"].as<bool>();
        node.is_scoring = row["is_scoring"].as<bool>();
        node.organization_note = row["organization_note"].as<std::string>();

        auto parse_string_array = [&reader](const std::string& text) {
            Json::Value value;
            std::string errors;
            std::istringstream stream(text);
            Json::parseFromStream(reader, stream, &value, &errors);
            std::vector<std::string> result;
            for (const auto& item : value) result.push_back(item.asString());
            return result;
        };
        node.bridge_type_ids =
            parse_string_array(row["bridge_type_ids"].as<std::string>());
        node.component_category_ids =
            parse_string_array(row["component_category_ids"].as<std::string>());
        {
            Json::Value value;
            std::string errors;
            std::istringstream stream(row["allowed_scales"].as<std::string>());
            Json::parseFromStream(reader, stream, &value, &errors);
            for (const auto& item : value) node.allowed_scales.push_back(item.asInt());
        }
        {
            Json::Value detail;
            std::string errors;
            std::istringstream stream(row["detail_json"].as<std::string>());
            Json::parseFromStream(reader, stream, &detail, &errors);
            node.h21_indicator_name = detail["h21_indicator_name"].asString();
            node.h21_source_table = detail["h21_source_table"].asString();
            node.uses_source_scale_descriptions =
                detail["uses_source_scale_descriptions"].asBool();
            for (const auto& scale :
                 detail["scale_descriptions"].getMemberNames()) {
                node.scale_descriptions.emplace(
                    std::stoi(scale),
                    detail["scale_descriptions"][scale].asString());
            }
            for (const auto& scale : detail["deduction_points"].getMemberNames()) {
                node.deduction_points.emplace(
                    std::stoi(scale),
                    detail["deduction_points"][scale].asInt());
            }
            if (detail["source_mappings"].isArray()) {
                for (const auto& value : detail["source_mappings"]) {
                    rating_tree::RatingTreeSourceMapping mapping;
                    mapping.source_group_id = value["source_group_id"].asString();
                    mapping.source_indicator_id =
                        value["source_indicator_id"].asString();
                    mapping.source_group_number =
                        value["source_group_number"].asString();
                    mapping.source_indicator_number =
                        value["source_indicator_number"].asString();
                    mapping.target_node_id = node.id;
                    node.source_mappings.push_back(std::move(mapping));
                }
            }
        }
        tree.nodes.emplace(node.id, std::move(node));
    }

    const auto aliases = db_client_->execSqlSync(
        "select alias_text,target_node_id::text as target_node_id,"
        "bridge_type_id,component_category_id from rating_tree_aliases "
        "where rating_tree_version_id=$1::uuid",
        version_id);
    for (const auto& row : aliases) {
        tree.aliases.push_back({
            row["alias_text"].as<std::string>(),
            row["target_node_id"].as<std::string>(),
            row["bridge_type_id"].as<std::string>(),
            row["component_category_id"].as<std::string>(),
        });
    }

    // 规则顺序稳定：排序交给 SQL，保证同一版本每次装载的规则序列一致。
    const auto keyword_rules = db_client_->execSqlSync(
        "select rule_key,target_node_id::text as target_node_id,bridge_type_id,"
        "component_category_id,auto_bind,sort_order,rule_note,"
        "array_to_json(positive_keywords)::text as positive_keywords,"
        "array_to_json(excluded_keywords)::text as excluded_keywords "
        "from rating_tree_keyword_rules where rating_tree_version_id=$1::uuid "
        "order by sort_order,rule_key",
        version_id);
    for (const auto& row : keyword_rules) {
        rating_tree::RatingTreeKeywordRule rule;
        rule.rule_id = row["rule_key"].as<std::string>();
        rule.target_node_id = row["target_node_id"].as<std::string>();
        rule.bridge_type_id = row["bridge_type_id"].as<std::string>();
        rule.component_category_id = row["component_category_id"].as<std::string>();
        rule.auto_bind = row["auto_bind"].as<bool>();
        rule.sort_order = row["sort_order"].as<int>();
        rule.rule_note = row["rule_note"].as<std::string>();
        const auto parse_keywords = [&reader](const std::string& text) {
            Json::Value value;
            std::string errors;
            std::istringstream stream(text);
            Json::parseFromStream(reader, stream, &value, &errors);
            std::vector<std::string> result;
            for (const auto& item : value) result.push_back(item.asString());
            return result;
        };
        rule.positive_keywords =
            parse_keywords(row["positive_keywords"].as<std::string>());
        rule.excluded_keywords =
            parse_keywords(row["excluded_keywords"].as<std::string>());
        tree.keyword_rules.push_back(std::move(rule));
    }
    return tree;
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
        "select p.id as profile_id, min(v.id::text)::uuid as tree_id "
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
