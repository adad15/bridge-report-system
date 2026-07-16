#include "bridge_report/review/WorkspaceModels.hpp"

#include <utility>

namespace bridge_report::review {
namespace {

Json::Value optional_string_json(const std::optional<std::string>& value) {
    return value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

Json::Value optional_number_json(const std::optional<double>& value) {
    return value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

template <typename Item>
Json::Value array_json(const std::vector<Item>& items) {
    Json::Value json(Json::arrayValue);
    for (const auto& item : items) {
        json.append(item.to_json());
    }
    return json;
}

}  // namespace

WorkspaceImportAction derive_workspace_import_action(
    const std::string_view import_status,
    const std::string_view temporary_source_status
) {
    if (import_status == "已上传" || import_status == "解析失败") {
        if (temporary_source_status == "已过期" || temporary_source_status == "已删除"
            || temporary_source_status == "待清理" || temporary_source_status == "清理中"
            || temporary_source_status == "清理失败") {
            return WorkspaceImportAction::Reupload;
        }
        return WorkspaceImportAction::Parse;
    }
    if (import_status == "待校对") {
        return WorkspaceImportAction::ContinueReview;
    }
    if (import_status == "已确认" || import_status == "已取消") {
        return WorkspaceImportAction::ViewResult;
    }
    return WorkspaceImportAction::None;
}

std::string_view workspace_import_action_name(const WorkspaceImportAction action) {
    switch (action) {
        case WorkspaceImportAction::Parse:
            return "parse";
        case WorkspaceImportAction::ContinueReview:
            return "continue_review";
        case WorkspaceImportAction::ViewResult:
            return "view_result";
        case WorkspaceImportAction::Reupload:
            return "reupload";
        case WorkspaceImportAction::None:
            return "none";
    }
    return "none";
}

Json::Value WorkspaceBridge::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["system_number"] = system_number;
    json["bridge_name"] = bridge_name;
    json["route_name"] = optional_string_json(route_name);
    json["status"] = status;
    return json;
}

Json::Value WorkspaceInspection::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["system_number"] = system_number;
    json["inspection_year"] = inspection_year;
    json["status"] = status;
    json["version_number"] = version_number;
    json["is_current"] = is_current;
    json["overall_score"] = optional_number_json(overall_score);
    json["overall_grade"] = optional_string_json(overall_grade);
    json["created_at"] = optional_string_json(created_at);
    json["updated_at"] = optional_string_json(updated_at);
    return json;
}

Json::Value WorkspaceStructureRating::to_json() const {
    Json::Value json;
    json["rating_level"] = rating_level;
    json["rating_item_name"] = rating_item_name;
    json["score"] = optional_number_json(score);
    json["grade"] = optional_string_json(grade);
    return json;
}

int WorkspacePendingSummary::total_count() const {
    return import_count + unbound_observation_count;
}

Json::Value WorkspacePendingSummary::to_json() const {
    Json::Value json;
    json["import_count"] = import_count;
    json["unbound_observation_count"] = unbound_observation_count;
    json["total_count"] = total_count();
    return json;
}

Json::Value WorkspaceDefectArchiveSummary::to_json() const {
    Json::Value json;
    json["component_count"] = component_count;
    json["thread_count"] = thread_count;
    json["unbound_observation_count"] = unbound_observation_count;
    return json;
}

Json::Value WorkspaceEditLock::to_json() const {
    Json::Value json;
    json["owner_username"] = owner_username;
    json["owner_display_name"] = owner_display_name;
    json["acquired_at"] = acquired_at;
    json["expires_at"] = expires_at;
    return json;
}

Json::Value WorkspaceImport::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["system_number"] = system_number;
    json["import_name"] = import_name;
    json["source_type"] = source_type;
    json["import_status"] = import_status;
    json["importer_name"] = optional_string_json(importer_name);
    json["created_at"] = optional_string_json(created_at);
    json["updated_at"] = optional_string_json(updated_at);
    json["error_message"] = optional_string_json(error_message);
    json["temporary_source_status"] = optional_string_json(temporary_source_status);
    json["temporary_source_expires_at"] = optional_string_json(temporary_source_expires_at);
    json["statistics"] = statistics.to_json();
    json["edit_lock"] = edit_lock.has_value() ? edit_lock->to_json() : Json::Value(Json::nullValue);
    json["available_action"] = std::string(workspace_import_action_name(
        derive_workspace_import_action(import_status, temporary_source_status.value_or(""))));
    return json;
}

Json::Value BridgeOverview::to_json() const {
    Json::Value json;
    json["bridge"] = bridge.to_json();
    json["latest_inspection"] = latest_inspection.has_value()
        ? latest_inspection->to_json()
        : Json::Value(Json::nullValue);
    json["recent_inspections"] = array_json(recent_inspections);
    json["structure_ratings"] = array_json(structure_ratings);
    json["pending"] = pending.to_json();
    json["defect_archive"] = defect_archive.to_json();
    return json;
}

Json::Value InspectionWorkspace::to_json() const {
    Json::Value json;
    json["bridge"] = bridge.to_json();
    json["inspection_year"] = inspection_year.to_json();
    json["imports"] = array_json(imports);
    json["pending"] = pending.to_json();
    return json;
}

}  // namespace bridge_report::review
