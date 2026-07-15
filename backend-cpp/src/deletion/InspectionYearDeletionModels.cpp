#include "bridge_report/deletion/InspectionYearDeletionModels.hpp"

#include <algorithm>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"

namespace bridge_report::deletion {
namespace {

template <typename T>
void sort_unique(std::vector<T>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

Json::Value string_array(std::vector<std::string> values) {
    sort_unique(values);
    Json::Value result(Json::arrayValue);
    for (const auto& value : values) result.append(value);
    return result;
}

Json::Value integer_array(std::vector<int> values) {
    sort_unique(values);
    Json::Value result(Json::arrayValue);
    for (const auto value : values) result.append(value);
    return result;
}

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

}  // namespace

Json::Value DeletionCounts::to_json() const {
    Json::Value json;
    json["inspection_versions"] = inspection_versions;
    json["import_records"] = import_records;
    json["defect_observations"] = defect_observations;
    json["defect_measurements"] = defect_measurements;
    json["defect_photos"] = defect_photos;
    json["condition_ratings"] = condition_ratings;
    json["archived_files_to_delete"] = archived_files_to_delete;
    json["shared_files_retained"] = shared_files_retained;
    json["defect_threads_affected"] = defect_threads_affected;
    json["defect_comparisons"] = defect_comparisons;
    return json;
}

Json::Value ActiveDeletionLock::to_json() const {
    Json::Value json;
    json["import_record_id"] = import_record_id;
    json["owner_username"] = owner_username;
    json["owner_display_name"] = owner_display_name;
    json["acquired_at"] = acquired_at;
    json["expires_at"] = expires_at;
    return json;
}

std::string InspectionYearDeletionPlan::confirmation_text() const {
    return "永久删除 " + std::to_string(inspection_year);
}

std::string InspectionYearDeletionPlan::impact_token() const {
    Json::Value canonical;
    canonical["bridge_id"] = bridge_id;
    canonical["inspection_year"] = inspection_year;
    canonical["version_numbers"] = integer_array(version_numbers);
    canonical["inspection_year_ids"] = string_array(inspection_year_ids);
    canonical["import_record_ids"] = string_array(import_record_ids);
    canonical["defect_observation_ids"] = string_array(defect_observation_ids);
    canonical["condition_rating_ids"] = string_array(condition_rating_ids);
    canonical["defect_thread_ids"] = string_array(defect_thread_ids);
    canonical["defect_comparison_ids"] = string_array(defect_comparison_ids);
    canonical["archived_file_ids_to_delete"] = string_array(archived_file_ids_to_delete);
    canonical["fingerprint_items"] = string_array(fingerprint_items);
    canonical["counts"] = counts.to_json();
    Json::Value locks(Json::arrayValue);
    auto sorted_locks = active_edit_locks;
    std::sort(sorted_locks.begin(), sorted_locks.end(), [](const auto& left, const auto& right) {
        return left.import_record_id < right.import_record_id;
    });
    for (const auto& lock : sorted_locks) locks.append(lock.to_json());
    canonical["active_edit_locks"] = std::move(locks);
    return "sha256:" + auth::sha256_hex(compact_json(canonical));
}

Json::Value InspectionYearDeletionPlan::to_public_json() const {
    Json::Value json;
    json["bridge"]["id"] = bridge_id;
    json["bridge"]["system_number"] = bridge_system_number;
    json["bridge"]["bridge_name"] = bridge_name;
    json["inspection_year"] = inspection_year;
    json["version_numbers"] = integer_array(version_numbers);
    json["counts"] = counts.to_json();
    Json::Value locks(Json::arrayValue);
    for (const auto& lock : active_edit_locks) locks.append(lock.to_json());
    json["active_edit_locks"] = std::move(locks);
    json["confirmation_text"] = confirmation_text();
    json["impact_token"] = impact_token();
    return json;
}

}  // namespace bridge_report::deletion
