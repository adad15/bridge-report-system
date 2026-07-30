#include "bridge_report/http/RatingTreeRoutes.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <sstream>
#include <tuple>
#include <utility>

#include <json/json.h>

#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

std::optional<db::AuthUser> require_user(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    auto user = authenticate_request(db_client, request);
    if (!user.has_value()) respond_unauthorized(callback);
    return user;
}

Json::Value parse_json_object(const std::string& text) {
    Json::Value value(Json::objectValue);
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &value, &errors) ||
        !value.isObject()) {
        return Json::Value(Json::objectValue);
    }
    return value;
}

Json::Value parse_json_array(const std::string& text) {
    Json::Value value(Json::arrayValue);
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &value, &errors) ||
        !value.isArray()) {
        return Json::Value(Json::arrayValue);
    }
    return value;
}

bool published_version_exists(
    const drogon::orm::DbClientPtr& db_client,
    const std::string& version_id) {
    return !db_client->execSqlSync(
        "select 1 from rating_tree_versions "
        "where id=$1::uuid and status='published'",
        version_id).empty();
}

Json::Value version_summary(const drogon::orm::Row& row) {
    Json::Value value;
    value["id"] = row["id"].as<std::string>();
    value["tree_code"] = row["tree_code"].as<std::string>();
    value["tree_name"] = row["tree_name"].as<std::string>();
    value["package_version"] = row["package_version"].as<std::string>();
    value["tree_content_checksum"] =
        row["tree_content_checksum"].as<std::string>();
    value["status"] = row["status"].as<std::string>();
    value["published_at"] = row["published_at"].as<std::string>();
    return value;
}

Json::Value node_summary(const drogon::orm::Row& row) {
    Json::Value value;
    value["id"] = row["id"].as<std::string>();
    value["node_key"] = row["node_key"].as<std::string>();
    value["parent_node_id"] = row["parent_node_id"].isNull()
        ? Json::Value()
        : Json::Value(row["parent_node_id"].as<std::string>());
    value["display_name"] = row["display_name"].as<std::string>();
    value["node_type"] = row["node_type"].as<std::string>();
    value["sort_order"] = row["sort_order"].as<int>();
    value["bridge_type_ids"] =
        parse_json_array(row["bridge_type_ids"].as<std::string>());
    value["component_category_ids"] =
        parse_json_array(row["component_category_ids"].as<std::string>());
    value["scoring_mode"] = row["scoring_mode"].as<std::string>();
    value["h21_indicator_id"] = row["h21_indicator_id"].isNull()
        ? Json::Value()
        : Json::Value(row["h21_indicator_id"].as<std::string>());
    value["is_selectable"] = row["is_selectable"].as<bool>();
    value["is_scoring"] = row["is_scoring"].as<bool>();
    return value;
}

const char* node_columns() {
    return "n.id::text as id,n.node_key,n.parent_node_id::text as parent_node_id,"
           "n.display_name,n.node_type,n.sort_order,"
           "array_to_json(n.bridge_type_ids)::text as bridge_type_ids,"
           "array_to_json(n.component_category_ids)::text as component_category_ids,"
           "n.scoring_mode,n.h21_indicator_id,n.is_selectable,n.is_scoring";
}

Json::Value node_path(
    const drogon::orm::DbClientPtr& db_client,
    const std::string& version_id,
    const std::string& node_id) {
    const auto rows = db_client->execSqlSync(
        "with recursive ancestors as ("
        "select id,parent_node_id,node_key,display_name,node_type,0 as depth "
        "from rating_tree_nodes "
        "where id=$2::uuid and rating_tree_version_id=$1::uuid "
        "union all "
        "select p.id,p.parent_node_id,p.node_key,p.display_name,p.node_type,a.depth+1 "
        "from rating_tree_nodes p join ancestors a on p.id=a.parent_node_id "
        "where p.rating_tree_version_id=$1::uuid"
        ") select id::text as id,node_key,display_name,node_type "
        "from ancestors order by depth desc",
        version_id,
        node_id);
    Json::Value path(Json::arrayValue);
    for (const auto& row : rows) {
        Json::Value item;
        item["id"] = row["id"].as<std::string>();
        item["node_key"] = row["node_key"].as<std::string>();
        item["display_name"] = row["display_name"].as<std::string>();
        item["node_type"] = row["node_type"].as<std::string>();
        path.append(std::move(item));
    }
    return path;
}

std::optional<Json::Value> full_node(
    const drogon::orm::DbClientPtr& db_client,
    const std::string& version_id,
    const std::string& node_id) {
    const auto rows = db_client->execSqlSync(
        std::string("select ") + node_columns() +
        ",n.organization_note,array_to_json(n.allowed_scales)::text as allowed_scales,"
        "n.detail_json::text as detail_json "
        "from rating_tree_nodes n "
        "where n.rating_tree_version_id=$1::uuid and n.id=$2::uuid",
        version_id,
        node_id);
    if (rows.empty()) return std::nullopt;
    auto value = node_summary(rows[0]);
    value["organization_note"] =
        rows[0]["organization_note"].as<std::string>();
    value["allowed_scales"] =
        parse_json_array(rows[0]["allowed_scales"].as<std::string>());
    const auto detail =
        parse_json_object(rows[0]["detail_json"].as<std::string>());
    value["h21_indicator_name"] = detail["h21_indicator_name"];
    value["h21_source_table"] = detail["h21_source_table"];
    value["scale_descriptions"] = detail["scale_descriptions"];
    value["deduction_points"] = detail["deduction_points"];
    value["path"] = node_path(db_client, version_id, node_id);
    value["sources"] = Json::Value(Json::arrayValue);
    const auto sources = db_client->execSqlSync(
        "select source_key,source_type,title,source_reference,source_rule_id "
        "from rating_tree_node_sources where rating_tree_node_id=$1::uuid "
        "order by source_type,source_key",
        node_id);
    for (const auto& source : sources) {
        Json::Value item;
        item["source_key"] = source["source_key"].as<std::string>();
        item["source_type"] = source["source_type"].as<std::string>();
        item["title"] = source["title"].as<std::string>();
        item["reference"] = source["source_reference"].as<std::string>();
        item["rule_id"] = source["source_rule_id"].isNull()
            ? Json::Value()
            : Json::Value(source["source_rule_id"].as<std::string>());
        value["sources"].append(std::move(item));
    }
    return value;
}

int parse_nonnegative(const std::string& value, const int fallback) {
    if (value.empty()) return fallback;
    int parsed = 0;
    const auto result =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} &&
            result.ptr == value.data() + value.size() && parsed >= 0
        ? parsed
        : fallback;
}

void respond_tree_not_found(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("rating_tree_not_found", "评定树版本不存在。"),
        drogon::k404NotFound);
}

}  // namespace

int parse_rating_tree_page_limit(const std::string& value) {
    return std::clamp(parse_nonnegative(value, 100), 1, 200);
}

int parse_rating_tree_page_offset(const std::string& value) {
    return parse_nonnegative(value, 0);
}

const std::vector<std::string>& rating_tree_route_methods() {
    static const std::vector<std::string> methods(6, "GET");
    return methods;
}

void register_rating_tree_routes(const drogon::orm::DbClientPtr& db_client) {
    const std::vector<std::string> paths{
        "/api/rating-trees",
        "/api/rating-trees/{version_id}",
        "/api/rating-trees/{version_id}/nodes",
        "/api/rating-trees/{version_id}/nodes/{node_id}",
        "/api/rating-trees/{version_id}/search",
        "/api/rating-trees/{version_id}/applicable-defects",
    };
    for (const auto& path : paths) register_options_handler(path);

    drogon::app().registerHandler(
        paths[0],
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                if (!require_user(db_client, request, callback).has_value()) return;
                const auto rows = db_client->execSqlSync(
                    "select id::text as id,tree_code,tree_name,package_version,"
                    "tree_content_checksum,status,published_at::text as published_at,"
                    "technical_condition_package_version,"
                    "maintenance_package_version "
                    "from rating_tree_versions where status='published' "
                    "order by published_at desc,tree_code,package_version");
                Json::Value body;
                body["versions"] = Json::Value(Json::arrayValue);
                for (const auto& row : rows) {
                    auto version = version_summary(row);
                    version["h21_package_version"] =
                        row["technical_condition_package_version"].as<std::string>();
                    version["maintenance_package_version"] =
                        row["maintenance_package_version"].as<std::string>();
                    body["versions"].append(std::move(version));
                }
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        paths[1],
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& version_id) {
            if (!is_valid_uuid(version_id)) {
                respond_tree_not_found(callback);
                return;
            }
            try {
                if (!require_user(db_client, request, callback).has_value()) return;
                const auto rows = db_client->execSqlSync(
                    "select v.id::text as id,v.tree_code,v.tree_name,v.package_version,"
                    "v.contract_version,v.tree_content_checksum,v.status,"
                    "v.published_at::text as published_at,"
                    "v.technical_condition_standard_id,"
                    "v.technical_condition_package_version,"
                    "v.technical_condition_content_checksum,"
                    "v.maintenance_standard_id,v.maintenance_package_version,"
                    "v.maintenance_content_checksum,v.organization_tree_code,"
                    "v.organization_package_version,"
                    "v.organization_content_checksum,"
                    "(select count(*) from rating_tree_nodes n "
                    "where n.rating_tree_version_id=v.id)::int as node_count "
                    "from rating_tree_versions v "
                    "where v.id=$1::uuid and v.status='published'",
                    version_id);
                if (rows.empty()) {
                    respond_tree_not_found(callback);
                    return;
                }
                Json::Value body;
                body["version"] = version_summary(rows[0]);
                body["version"]["contract_version"] =
                    rows[0]["contract_version"].as<int>();
                body["version"]["node_count"] = rows[0]["node_count"].as<int>();
                body["version"]["sources"] = Json::Value(Json::arrayValue);
                const std::vector<std::tuple<std::string, std::string, std::string>>
                    sources{
                        {"technical_condition",
                         rows[0]["technical_condition_standard_id"].as<std::string>(),
                         rows[0]["technical_condition_package_version"].as<std::string>()},
                        {"maintenance",
                         rows[0]["maintenance_standard_id"].as<std::string>(),
                         rows[0]["maintenance_package_version"].as<std::string>()},
                        {"organization",
                         rows[0]["organization_tree_code"].as<std::string>(),
                         rows[0]["organization_package_version"].as<std::string>()},
                    };
                const std::vector<std::string> checksums{
                    rows[0]["technical_condition_content_checksum"].as<std::string>(),
                    rows[0]["maintenance_content_checksum"].as<std::string>(),
                    rows[0]["organization_content_checksum"].as<std::string>(),
                };
                for (std::size_t index = 0; index < sources.size(); ++index) {
                    Json::Value source;
                    source["source_type"] = std::get<0>(sources[index]);
                    source["source_id"] = std::get<1>(sources[index]);
                    source["package_version"] = std::get<2>(sources[index]);
                    source["content_checksum"] = checksums[index];
                    body["version"]["sources"].append(std::move(source));
                }
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        paths[2],
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& version_id) {
            if (!is_valid_uuid(version_id)) {
                respond_tree_not_found(callback);
                return;
            }
            const auto parent = request->getParameter("parent_id");
            const bool roots_only = parent == "root";
            if (!parent.empty() && !roots_only && !is_valid_uuid(parent)) {
                respond_json(
                    callback,
                    make_error_body("rating_tree_parent_invalid", "父节点参数无效。"),
                    drogon::k400BadRequest);
                return;
            }
            try {
                if (!require_user(db_client, request, callback).has_value()) return;
                if (!published_version_exists(db_client, version_id)) {
                    respond_tree_not_found(callback);
                    return;
                }
                const auto limit =
                    parse_rating_tree_page_limit(request->getParameter("limit"));
                const auto offset =
                    parse_rating_tree_page_offset(request->getParameter("offset"));
                auto parent_id = roots_only ? std::string{} : parent;
                const auto rows = db_client->execSqlSync(
                    std::string("select ") + node_columns() +
                    " from rating_tree_nodes n where n.rating_tree_version_id=$1::uuid "
                    "and (($2::boolean and n.parent_node_id is null) "
                    "or (not $2::boolean and "
                    "($3='' or n.parent_node_id=nullif($3,'')::uuid))) "
                    "order by n.sort_order,n.node_key "
                    "limit $4::integer offset $5::integer",
                    version_id,
                    roots_only,
                    parent_id,
                    limit,
                    offset);
                Json::Value body;
                body["nodes"] = Json::Value(Json::arrayValue);
                for (const auto& row : rows) {
                    body["nodes"].append(node_summary(row));
                }
                body["limit"] = limit;
                body["offset"] = offset;
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        paths[3],
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& version_id, const std::string& node_id) {
            if (!is_valid_uuid(version_id) || !is_valid_uuid(node_id)) {
                respond_tree_not_found(callback);
                return;
            }
            try {
                if (!require_user(db_client, request, callback).has_value()) return;
                if (!published_version_exists(db_client, version_id)) {
                    respond_tree_not_found(callback);
                    return;
                }
                const auto node = full_node(db_client, version_id, node_id);
                if (!node.has_value()) {
                    respond_json(
                        callback,
                        make_error_body("rating_tree_node_not_found", "评定树节点不存在。"),
                        drogon::k404NotFound);
                    return;
                }
                Json::Value body;
                body["node"] = *node;
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    const auto register_search_like =
        [db_client](const std::string& path, const bool applicable_only) {
        drogon::app().registerHandler(
            path,
            [db_client, applicable_only](
                const drogon::HttpRequestPtr& request,
                HttpCallback&& callback,
                const std::string& version_id) {
                if (!is_valid_uuid(version_id)) {
                    respond_tree_not_found(callback);
                    return;
                }
                const auto query = request->getParameter("q");
                const auto bridge_type = request->getParameter("bridge_type_id");
                const auto component = request->getParameter("component_category_id");
                if (applicable_only &&
                    (bridge_type.empty() || component.empty())) {
                    respond_json(
                        callback,
                        make_error_body(
                            "rating_tree_scope_required",
                            "必须同时提供桥型和构件类别。"),
                        drogon::k400BadRequest);
                    return;
                }
                if (!applicable_only && query.empty()) {
                    respond_json(
                        callback,
                        make_error_body(
                            "rating_tree_search_query_required",
                            "搜索关键字不能为空。"),
                        drogon::k400BadRequest);
                    return;
                }
                try {
                    if (!require_user(db_client, request, callback).has_value()) return;
                    if (!published_version_exists(db_client, version_id)) {
                        respond_tree_not_found(callback);
                        return;
                    }
                    const auto limit =
                        parse_rating_tree_page_limit(request->getParameter("limit"));
                    const auto rows = [&]() {
                        if (applicable_only) {
                            return db_client->execSqlSync(
                            std::string("select ") + node_columns() +
                            " from rating_tree_nodes n "
                            "where n.rating_tree_version_id=$1::uuid "
                            "and n.node_type='defect' and n.is_selectable "
                            "and n.bridge_type_ids @> array[$2]::text[] "
                            "and n.component_category_ids @> array[$3]::text[] "
                            "order by n.sort_order,n.display_name "
                            "limit $4::integer",
                            version_id,
                            bridge_type,
                            component,
                            limit);
                        }
                        return db_client->execSqlSync(
                            std::string("select ") + node_columns() +
                            " from rating_tree_nodes n "
                            "where n.rating_tree_version_id=$1::uuid "
                            "and n.display_name ilike ('%'||$2||'%') "
                            "order by n.is_selectable desc,n.sort_order,n.display_name "
                            "limit $3::integer",
                            version_id,
                            query,
                            limit);
                    }();
                    Json::Value body;
                    body["nodes"] = Json::Value(Json::arrayValue);
                    for (const auto& row : rows) {
                        auto item = node_summary(row);
                        item["path"] = node_path(
                            db_client, version_id, row["id"].as<std::string>());
                        body["nodes"].append(std::move(item));
                    }
                    respond_json(callback, body);
                } catch (...) {
                    respond_db_unavailable(callback);
                }
            },
            {drogon::Get});
    };
    register_search_like(paths[4], false);
    register_search_like(paths[5], true);
}

}  // namespace bridge_report::http
