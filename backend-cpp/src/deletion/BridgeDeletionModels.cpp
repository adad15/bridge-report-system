#include "bridge_report/deletion/BridgeDeletionModels.hpp"

#include <algorithm>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"

namespace bridge_report::deletion {
namespace {

Json::Value strings(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    Json::Value result(Json::arrayValue);
    for (const auto& value : values) result.append(value);
    return result;
}

std::string compact(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

void add_optional(Json::Value& json, const char* key, const std::optional<std::string>& value) {
    json[key] = value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

}  // namespace

Json::Value BridgeDeletionCounts::to_json() const {
    Json::Value value;
    value["inspection_years"] = inspection_years;
    value["inspection_versions"] = inspection_versions;
    value["import_records"] = import_records;
    value["bridge_aliases"] = bridge_aliases;
    value["bridge_components"] = bridge_components;
    value["component_aliases"] = component_aliases;
    value["component_generation_batches"] = component_generation_batches;
    value["component_inventory_revisions"] = component_inventory_revisions;
    value["component_inventory_entries"] = component_inventory_entries;
    value["component_standard_mappings"] = component_standard_mappings;
    value["defect_threads"] = defect_threads;
    value["defect_observations"] = defect_observations;
    value["defect_measurements"] = defect_measurements;
    value["defect_photos"] = defect_photos;
    value["condition_ratings"] = condition_ratings;
    value["defect_comparisons"] = defect_comparisons;
    value["archived_files_to_delete"] = archived_files_to_delete;
    value["temporary_source_files_to_delete"] = temporary_source_files_to_delete;
    value["shared_files_retained"] = shared_files_retained;
    return value;
}

BridgeDeletionCounts& BridgeDeletionCounts::operator+=(const BridgeDeletionCounts& other) {
    inspection_years += other.inspection_years;
    inspection_versions += other.inspection_versions;
    import_records += other.import_records;
    bridge_aliases += other.bridge_aliases;
    bridge_components += other.bridge_components;
    component_aliases += other.component_aliases;
    component_generation_batches += other.component_generation_batches;
    component_inventory_revisions += other.component_inventory_revisions;
    component_inventory_entries += other.component_inventory_entries;
    component_standard_mappings += other.component_standard_mappings;
    defect_threads += other.defect_threads;
    defect_observations += other.defect_observations;
    defect_measurements += other.defect_measurements;
    defect_photos += other.defect_photos;
    condition_ratings += other.condition_ratings;
    defect_comparisons += other.defect_comparisons;
    archived_files_to_delete += other.archived_files_to_delete;
    temporary_source_files_to_delete += other.temporary_source_files_to_delete;
    shared_files_retained += other.shared_files_retained;
    return *this;
}

std::string BridgeDeletionPlan::impact_token() const {
    Json::Value canonical;
    canonical["bridge_id"] = bridge_id;
    canonical["bridge_system_number"] = bridge_system_number;
    canonical["fingerprint_items"] = strings(fingerprint_items);
    canonical["counts"] = counts.to_json();
    Json::Value locks(Json::arrayValue);
    auto sorted = active_edit_locks;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.import_record_id < b.import_record_id;
    });
    for (const auto& lock : sorted) locks.append(lock.to_json());
    canonical["active_edit_locks"] = std::move(locks);
    return "sha256:" + auth::sha256_hex(compact(canonical));
}

Json::Value BridgeDeletionPlan::to_public_json() const {
    Json::Value value;
    value["bridge"]["id"] = bridge_id;
    value["bridge"]["system_number"] = bridge_system_number;
    value["bridge"]["bridge_name"] = bridge_name;
    add_optional(value["bridge"], "route_number", route_number);
    add_optional(value["bridge"], "route_name", route_name);
    add_optional(value["bridge"], "station_mark", station_mark);
    value["bridge"]["status"] = status;
    value["counts"] = counts.to_json();
    Json::Value locks(Json::arrayValue);
    for (const auto& lock : active_edit_locks) locks.append(lock.to_json());
    value["active_edit_locks"] = std::move(locks);
    value["impact_token"] = impact_token();
    return value;
}

std::string bridge_deletion_confirmation_text(std::vector<std::string> system_numbers) {
    std::sort(system_numbers.begin(), system_numbers.end());
    system_numbers.erase(std::unique(system_numbers.begin(), system_numbers.end()), system_numbers.end());
    std::string result = "永久删除 ";
    for (std::size_t i = 0; i < system_numbers.size(); ++i) {
        if (i != 0) result += "、";
        result += system_numbers[i];
    }
    return result;
}

}  // namespace bridge_report::deletion
