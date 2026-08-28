#include "bridge_report/review/ReviewModels.hpp"

#include "bridge_report/review/ReviewStatistics.hpp"

namespace bridge_report::review {

Json::Value BridgeSummary::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["system_number"] = system_number;
    json["bridge_name"] = bridge_name;
    json["route_name"] = route_name.has_value() ? Json::Value(*route_name) : Json::Value(Json::nullValue);
    json["status"] = status;
    json["bridge_scale"] = bridge_scale.has_value() ? Json::Value(*bridge_scale) : Json::Value(Json::nullValue);
    json["latest_inspection_year"] = latest_inspection_year.has_value()
        ? Json::Value(*latest_inspection_year) : Json::Value(Json::nullValue);
    json["latest_overall_score"] = latest_overall_score.has_value()
        ? Json::Value(*latest_overall_score) : Json::Value(Json::nullValue);
    json["latest_overall_grade"] = latest_overall_grade.has_value()
        ? Json::Value(*latest_overall_grade) : Json::Value(Json::nullValue);
    json["pending_count"] = pending_count;
    return json;
}

Json::Value InspectionYearSummary::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["system_number"] = system_number;
    json["inspection_year"] = inspection_year;
    json["status"] = status;
    json["version_number"] = version_number;
    json["is_current"] = is_current;
    return json;
}

Json::Value ImportRecordSummary::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["system_number"] = system_number;
    json["import_name"] = import_name;
    json["source_type"] = source_type;
    json["import_status"] = import_status;
    json["inspection_year_id"] =
        inspection_year_id.has_value() ? Json::Value(*inspection_year_id) : Json::Value(Json::nullValue);
    json["importer_name"] =
        importer_name.has_value() ? Json::Value(*importer_name) : Json::Value(Json::nullValue);
    json["created_at"] = created_at;
    if (edit_lock_owner_username.has_value() && edit_lock_owner_display_name.has_value()
        && edit_lock_acquired_at.has_value() && edit_lock_expires_at.has_value()) {
        Json::Value edit_lock;
        edit_lock["owner_username"] = *edit_lock_owner_username;
        edit_lock["owner_display_name"] = *edit_lock_owner_display_name;
        edit_lock["acquired_at"] = *edit_lock_acquired_at;
        edit_lock["expires_at"] = *edit_lock_expires_at;
        json["edit_lock"] = std::move(edit_lock);
    } else {
        json["edit_lock"] = Json::Value(Json::nullValue);
    }
    return json;
}

Json::Value build_review_response(
    const ImportRecordDetail& detail,
    const Json::Value& parsed_result,
    const ReviewStatistics& statistics,
    bool has_current_annual_facts,
    std::string_view contract_compatibility
) {
    Json::Value import_record;
    import_record["id"] = detail.id;
    import_record["system_number"] = detail.system_number;
    import_record["import_name"] = detail.import_name;
    import_record["source_type"] = detail.source_type;
    import_record["import_status"] = detail.import_status;
    // 客户端下一次写草稿要拿它做 If-Match（§8.0）。
    import_record["draft_version"] = detail.draft_version;
    import_record["importer_name"] =
        detail.importer_name.has_value() ? Json::Value(*detail.importer_name) : Json::Value(Json::nullValue);
    import_record["importer_version"] =
        detail.importer_version.has_value() ? Json::Value(*detail.importer_version) : Json::Value(Json::nullValue);
    import_record["created_at"] = detail.created_at;
    import_record["updated_at"] = detail.updated_at;

    Json::Value bridge;
    bridge["id"] = detail.bridge_id;
    bridge["system_number"] = detail.bridge_system_number;
    bridge["bridge_name"] = detail.bridge_name;
    bridge["route_name"] =
        detail.bridge_route_name.has_value() ? Json::Value(*detail.bridge_route_name) : Json::Value(Json::nullValue);

    Json::Value inspection_year(Json::nullValue);
    if (detail.inspection_year_id.has_value()) {
        inspection_year = Json::Value(Json::objectValue);
        inspection_year["id"] = *detail.inspection_year_id;
        inspection_year["system_number"] =
            detail.inspection_year_system_number.has_value() ? *detail.inspection_year_system_number : "";
        inspection_year["inspection_year"] =
            detail.inspection_year.has_value() ? *detail.inspection_year : 0;
        inspection_year["status"] =
            detail.inspection_year_status.has_value() ? *detail.inspection_year_status : "";
        inspection_year["version_number"] =
            detail.inspection_year_version_number.has_value() ? *detail.inspection_year_version_number : 0;
        inspection_year["is_current"] =
            detail.inspection_year_is_current.has_value() && *detail.inspection_year_is_current;
    }

    // 重开校对现场：非重开态为 null；重开态携带谁/何时/什么范围，
    // 前端据此决定逐病害可编辑范围与「放弃修改」按钮。
    Json::Value reopen(Json::nullValue);
    if (detail.reopened_at.has_value()) {
        reopen = Json::Value(Json::objectValue);
        reopen["reopened_at"] = *detail.reopened_at;
        reopen["reopened_by_username"] = detail.reopened_by_username.value_or("");
        reopen["scope"] = detail.reopen_scope.value_or("");
    }

    Json::Value body;
    body["import_record"] = import_record;
    body["bridge"] = bridge;
    body["inspection_year"] = inspection_year;
    body["parsed_result"] = parsed_result;
    body["statistics"] = statistics.to_json();
    body["has_current_annual_facts"] = has_current_annual_facts;
    body["contract_compatibility"] = std::string(contract_compatibility);
    if (detail.technical_standard_package_id.has_value()) {
        Json::Value standard(Json::objectValue);
        standard["package_id"] = *detail.technical_standard_package_id;
        standard["standard_code"] =
            detail.technical_standard_code.value_or("");
        standard["standard_name"] =
            detail.technical_standard_name.value_or("");
        standard["official_edition"] =
            detail.technical_standard_official_edition.value_or("");
        standard["package_version"] =
            detail.technical_standard_package_version.value_or("");
        body["technical_condition_standard"] = std::move(standard);
    } else {
        body["technical_condition_standard"] = Json::Value();
    }
    if (detail.rating_tree_version_id.has_value()) {
        Json::Value tree(Json::objectValue);
        tree["version_id"] = *detail.rating_tree_version_id;
        tree["tree_name"] = detail.rating_tree_name.value_or("");
        tree["package_version"] =
            detail.rating_tree_package_version.value_or("");
        tree["content_checksum"] =
            detail.rating_tree_content_checksum.value_or("");
        body["rating_tree"] = std::move(tree);
    } else {
        body["rating_tree"] = Json::Value();
    }
    body["reopen"] = reopen;
    return body;
}

std::optional<int> resolve_effective_inspection_year(
    const ImportRecordDetail& detail,
    const Json::Value& parsed_result
) {
    if (detail.inspection_year.has_value()) {
        return detail.inspection_year;
    }

    if (parsed_result.isObject() && parsed_result.isMember("inspection") && parsed_result["inspection"].isObject()
        && parsed_result["inspection"].isMember("inspection_year")
        && parsed_result["inspection"]["inspection_year"].isInt()) {
        return parsed_result["inspection"]["inspection_year"].asInt();
    }

    return std::nullopt;
}

}  // 命名空间 bridge_report::review
