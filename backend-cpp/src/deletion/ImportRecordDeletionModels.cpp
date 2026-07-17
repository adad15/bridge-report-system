#include "bridge_report/deletion/ImportRecordDeletionModels.hpp"

#include <algorithm>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"

namespace bridge_report::deletion {
namespace {

Json::Value sorted_strings(std::vector<std::string> values) {
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

}  // namespace

Json::Value ImportRecordDeletionCounts::to_json() const {
    Json::Value value;
    value["defects"] = defects;
    value["photos"] = photos;
    value["rating_items"] = rating_items;
    value["parsed_images"] = parsed_images;
    value["archived_files_to_delete"] = archived_files_to_delete;
    value["temporary_word_files_to_delete"] = temporary_word_files_to_delete;
    value["parse_work_directories_to_delete"] = parse_work_directories_to_delete;
    value["shared_files_retained"] = shared_files_retained;
    value["formal_fact_references"] = formal_fact_references;
    return value;
}

bool ImportRecordDeletionPlan::status_allows_delete() const {
    return import_status == "已上传" || import_status == "解析中" ||
           import_status == "解析失败" || import_status == "待校对" ||
           import_status == "已取消";
}

bool ImportRecordDeletionPlan::can_delete() const {
    return status_allows_delete() && active_edit_locks.empty() && counts.formal_fact_references == 0;
}

std::optional<std::string> ImportRecordDeletionPlan::block_code() const {
    if (!status_allows_delete()) return "import_record_not_deletable";
    if (!active_edit_locks.empty()) return "import_record_edit_locked";
    if (counts.formal_fact_references > 0) return "import_record_has_formal_facts";
    return std::nullopt;
}

std::string ImportRecordDeletionPlan::confirmation_text() const {
    return "永久删除 " + import_system_number;
}

std::string ImportRecordDeletionPlan::impact_token() const {
    Json::Value canonical;
    canonical["import_record_id"] = import_record_id;
    canonical["system_number"] = import_system_number;
    canonical["status"] = import_status;
    canonical["updated_at"] = updated_at;
    canonical["counts"] = counts.to_json();
    canonical["fingerprint_items"] = sorted_strings(fingerprint_items);
    Json::Value locks(Json::arrayValue);
    auto sorted = active_edit_locks;
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        return left.import_record_id < right.import_record_id;
    });
    for (const auto& lock : sorted) locks.append(lock.to_json());
    canonical["active_edit_locks"] = std::move(locks);
    return "sha256:" + auth::sha256_hex(compact(canonical));
}

Json::Value ImportRecordDeletionPlan::to_public_json() const {
    Json::Value value;
    value["import_record"]["id"] = import_record_id;
    value["import_record"]["system_number"] = import_system_number;
    value["import_record"]["import_name"] = import_name;
    value["import_record"]["status"] = import_status;
    value["import_record"]["source_type"] = source_type;
    value["bridge"]["id"] = bridge_id;
    value["bridge"]["system_number"] = bridge_system_number;
    value["bridge"]["bridge_name"] = bridge_name;
    value["inspection_year"]["id"] = inspection_year_id;
    value["inspection_year"]["year"] = inspection_year;
    value["inspection_year"]["version_number"] = inspection_version;
    value["counts"] = counts.to_json();
    Json::Value locks(Json::arrayValue);
    for (const auto& lock : active_edit_locks) locks.append(lock.to_json());
    value["active_edit_locks"] = std::move(locks);
    value["can_delete"] = can_delete();
    if (const auto code = block_code(); code.has_value()) value["block_code"] = *code;
    else value["block_code"] = Json::nullValue;
    value["confirmation_text"] = confirmation_text();
    value["impact_token"] = impact_token();
    return value;
}

}  // namespace bridge_report::deletion
