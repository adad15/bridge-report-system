#include "bridge_report/db/ReportTemplateRepository.hpp"

#include <utility>

#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>
#include <json/json.h>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

Json::Value parse_json_object(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &value, &errors) || !value.isObject()) {
        return Json::Value(Json::objectValue);
    }
    return value;
}

std::optional<std::string> optional_text(const drogon::orm::Row& row, const char* column) {
    const auto field = row[column];
    if (field.isNull()) return std::nullopt;
    return field.as<std::string>();
}

constexpr const char* kColumns =
    "t.id::text as id, t.template_code, t.template_name, t.description, t.contract_type, "
    "t.file_id::text as file_id, t.file_checksum, af.current_file_name as file_name, "
    "t.contract_config_json::text as contract_config_json, t.validation_status, "
    "t.validation_result_json::text as validation_result_json, t.is_enabled, t.is_default, "
    "u.display_name as updated_by_display_name, t.updated_at::text as updated_at, "
    "(select count(*) from inspection_report_settings s where s.template_id=t.id) as usage_count ";

constexpr const char* kFrom =
    "from report_templates t "
    "join archived_files af on af.id=t.file_id "
    "left join users u on u.id=t.updated_by_user_id ";

report::ReportTemplate read_template(const drogon::orm::Row& row) {
    report::ReportTemplate item;
    item.id = row["id"].as<std::string>();
    item.template_code = row["template_code"].as<std::string>();
    item.template_name = row["template_name"].as<std::string>();
    item.description = optional_text(row, "description");
    item.contract_type = row["contract_type"].as<std::string>();
    item.file_id = row["file_id"].as<std::string>();
    item.file_checksum = row["file_checksum"].as<std::string>();
    item.file_name = row["file_name"].as<std::string>();
    item.contract_config = parse_json_object(row["contract_config_json"].as<std::string>());
    item.validation_status = row["validation_status"].as<std::string>();
    item.validation_result = parse_json_object(row["validation_result_json"].as<std::string>());
    item.is_enabled = row["is_enabled"].as<bool>();
    item.is_default = row["is_default"].as<bool>();
    item.updated_by_display_name = optional_text(row, "updated_by_display_name");
    item.updated_at = row["updated_at"].as<std::string>();
    item.usage_count = row["usage_count"].as<int>();
    return item;
}

/// 登记一份模板文件到 archived_files。模板不属于任何桥梁或年度，两个外键都留空。
std::string insert_template_file(
    const TransactionPtr& tx, const report::TemplateFileInput& file) {
    const auto rows = tx->execSqlSync(
        "insert into archived_files "
        "(original_file_name, current_file_name, storage_relative_path, file_type, "
        " file_purpose, file_extension, file_size_bytes, file_hash) "
        "values ($1, $1, $2, '模板', '报告模板', 'docx', $3, $4) "
        "returning id::text as id",
        file.original_file_name, file.storage_relative_path,
        static_cast<int64_t>(file.size_bytes), file.checksum);
    return rows[0]["id"].as<std::string>();
}

/// 这份归档文件是否还被别的业务引用。用于判断能不能把它从磁盘上删掉（设计 §17.4）。
bool file_still_referenced(
    const TransactionPtr& tx, const std::string& file_id, const std::string& excluding_template_id) {
    const auto rows = tx->execSqlSync(
        "select ("
        " exists(select 1 from report_templates x where x.file_id=$1::uuid and x.id<>$2::uuid) or"
        " exists(select 1 from import_records x where x.main_file_id=$1::uuid) or"
        " exists(select 1 from import_record_files x where x.archived_file_id=$1::uuid) or"
        " exists(select 1 from bridge_aliases x where x.source_file_id=$1::uuid) or"
        " exists(select 1 from component_aliases x where x.source_file_id=$1::uuid) or"
        " exists(select 1 from defect_observations x where x.source_file_id=$1::uuid) or"
        " exists(select 1 from defect_photos x where x.archived_file_id=$1::uuid or x.source_file_id=$1::uuid) or"
        " exists(select 1 from condition_ratings x where x.source_file_id=$1::uuid)"
        ") as referenced",
        file_id, excluding_template_id);
    return rows[0]["referenced"].as<bool>();
}

}  // namespace

ReportTemplateRepository::ReportTemplateRepository(drogon::orm::DbClientPtr client)
    : client_(std::move(client)) {}

std::vector<report::ReportTemplate> ReportTemplateRepository::list(bool only_enabled) const {
    const std::string sql = std::string("select ") + kColumns + kFrom +
        (only_enabled ? "where t.is_enabled " : "") +
        "order by t.is_default desc, t.is_enabled desc, t.template_name, t.id";
    std::vector<report::ReportTemplate> items;
    for (const auto& row : client_->execSqlSync(sql)) {
        items.push_back(read_template(row));
    }
    return items;
}

std::optional<report::ReportTemplate> ReportTemplateRepository::find(
    const std::string& id) const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kColumns + kFrom + "where t.id=$1::uuid", id);
    if (rows.empty()) return std::nullopt;
    return read_template(rows[0]);
}

std::optional<report::ReportTemplate> ReportTemplateRepository::find_default() const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kColumns + kFrom + "where t.is_default");
    if (rows.empty()) return std::nullopt;
    return read_template(rows[0]);
}

std::optional<std::string> ReportTemplateRepository::find_file_relative_path(
    const std::string& id) const {
    const auto rows = client_->execSqlSync(
        "select af.storage_relative_path from report_templates t "
        "join archived_files af on af.id = t.file_id where t.id = $1::uuid",
        id);
    if (rows.empty() || rows[0]["storage_relative_path"].isNull()) return std::nullopt;
    return rows[0]["storage_relative_path"].as<std::string>();
}

std::optional<report::ReportTemplate> ReportTemplateRepository::create(
    const report::ReportTemplateInput& input,
    const report::TemplateFileInput& file,
    const report::TemplateValidationOutcome& validation,
    report::TemplateWriteStatus& status) const {
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = client_->newTransaction(latch->callback());
        const auto duplicate = tx->execSqlSync(
            "select 1 from report_templates where template_code=$1", input.template_code);
        if (!duplicate.empty()) {
            tx->rollback();
            status = report::TemplateWriteStatus::DuplicateCode;
            return std::nullopt;
        }
        const auto file_id = insert_template_file(tx, file);
        // 新模板一律先停用：启用要等管理员看过校验结论、并且以后还要过测试生成
        // （设计 §23.1）。校验不通过时更不能自动上线。
        const auto inserted = tx->execSqlSync(
            "insert into report_templates "
            "(template_code, template_name, description, contract_type, file_id, file_checksum, "
            " contract_config_json, validation_status, validation_result_json, "
            " is_enabled, is_default, updated_by_user_id) "
            "values ($1, $2, nullif($3,''), $4, $5::uuid, $6, $7::jsonb, $8, $9::jsonb, "
            "        false, false, $10::uuid) "
            "returning id::text as id",
            input.template_code, input.template_name, input.description.value_or(""),
            input.contract_type, file_id, file.checksum,
            compact_json(input.contract_config),
            validation.is_valid ? "valid" : "invalid",
            compact_json(validation.result), input.updated_by_user_id);
        const auto id = inserted[0]["id"].as<std::string>();
        tx.reset();
        if (!latch->wait()) {
            status = report::TemplateWriteStatus::NotFound;
            return std::nullopt;
        }
        status = report::TemplateWriteStatus::Ok;
        return find(id);
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        status = report::TemplateWriteStatus::NotFound;
        return std::nullopt;
    }
}

std::optional<report::ReportTemplate> ReportTemplateRepository::update(
    const std::string& id,
    const report::ReportTemplateInput& input,
    const report::TemplateValidationOutcome& validation,
    report::TemplateWriteStatus& status) const {
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = client_->newTransaction(latch->callback());
        const auto existing = tx->execSqlSync(
            "select is_default from report_templates where id=$1::uuid for update", id);
        if (existing.empty()) {
            tx->rollback();
            status = report::TemplateWriteStatus::NotFound;
            return std::nullopt;
        }
        const auto duplicate = tx->execSqlSync(
            "select 1 from report_templates where template_code=$1 and id<>$2::uuid",
            input.template_code, id);
        if (!duplicate.empty()) {
            tx->rollback();
            status = report::TemplateWriteStatus::DuplicateCode;
            return std::nullopt;
        }
        // 编号格式是模板配置的一部分，改了配置校验结论就可能翻转。若翻成不通过，
        // 必须连带下线——否则一套已知不合规的模板会继续被普通用户选中。
        const bool valid = validation.is_valid;
        tx->execSqlSync(
            "update report_templates set template_code=$2, template_name=$3, "
            " description=nullif($4,''), contract_type=$5, contract_config_json=$6::jsonb, "
            " validation_status=$7, validation_result_json=$8::jsonb, "
            " is_enabled=(is_enabled and $9), is_default=(is_default and $9), "
            " updated_by_user_id=$10::uuid, updated_at=now() "
            "where id=$1::uuid",
            id, input.template_code, input.template_name, input.description.value_or(""),
            input.contract_type, compact_json(input.contract_config),
            valid ? "valid" : "invalid", compact_json(validation.result), valid,
            input.updated_by_user_id);
        tx.reset();
        if (!latch->wait()) {
            status = report::TemplateWriteStatus::NotFound;
            return std::nullopt;
        }
        status = report::TemplateWriteStatus::Ok;
        return find(id);
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        status = report::TemplateWriteStatus::NotFound;
        return std::nullopt;
    }
}

report::TemplateFileReplacement ReportTemplateRepository::replace_file(
    const std::string& id,
    const report::TemplateFileInput& file,
    const report::TemplateValidationOutcome& validation,
    const std::string& updated_by_user_id) const {
    report::TemplateFileReplacement replacement;
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = client_->newTransaction(latch->callback());
        const auto existing = tx->execSqlSync(
            "select t.file_id::text as file_id, af.storage_relative_path "
            "from report_templates t join archived_files af on af.id=t.file_id "
            "where t.id=$1::uuid for update of t",
            id);
        if (existing.empty()) {
            tx->rollback();
            replacement.status = report::TemplateWriteStatus::NotFound;
            return replacement;
        }
        const auto old_file_id = existing[0]["file_id"].as<std::string>();
        const auto old_path = existing[0]["storage_relative_path"].as<std::string>();

        const auto new_file_id = insert_template_file(tx, file);
        const bool valid = validation.is_valid;
        // 先原子把引用切到新文件，再回头处理旧文件——顺序反了会撞 RESTRICT（设计 §17.4）。
        tx->execSqlSync(
            "update report_templates set file_id=$2::uuid, file_checksum=$3, "
            " validation_status=$4, validation_result_json=$5::jsonb, "
            " is_enabled=(is_enabled and $6), is_default=(is_default and $6), "
            " updated_by_user_id=$7::uuid, updated_at=now() "
            "where id=$1::uuid",
            id, new_file_id, file.checksum, valid ? "valid" : "invalid",
            compact_json(validation.result), valid, updated_by_user_id);

        if (!file_still_referenced(tx, old_file_id, id)) {
            tx->execSqlSync("delete from archived_files where id=$1::uuid", old_file_id);
            replacement.obsolete_storage_relative_path = old_path;
        }
        tx.reset();
        if (!latch->wait()) {
            replacement.status = report::TemplateWriteStatus::NotFound;
            replacement.obsolete_storage_relative_path.reset();
            return replacement;
        }
        replacement.status = report::TemplateWriteStatus::Ok;
        return replacement;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        replacement.status = report::TemplateWriteStatus::NotFound;
        replacement.obsolete_storage_relative_path.reset();
        return replacement;
    }
}

report::TemplateWriteStatus ReportTemplateRepository::set_enabled(
    const std::string& id, bool enabled, const std::string& updated_by_user_id) const {
    try {
        const auto existing = client_->execSqlSync(
            "select validation_status, is_default from report_templates where id=$1::uuid", id);
        if (existing.empty()) return report::TemplateWriteStatus::NotFound;
        if (enabled && existing[0]["validation_status"].as<std::string>() != "valid") {
            return report::TemplateWriteStatus::NotValidated;
        }
        // 停用默认模板会让普通用户选不到任何模板，而且违反"默认必然启用"的约束。
        if (!enabled && existing[0]["is_default"].as<bool>()) {
            return report::TemplateWriteStatus::IsDefaultTemplate;
        }
        client_->execSqlSync(
            "update report_templates set is_enabled=$2, updated_by_user_id=$3::uuid, "
            " updated_at=now() where id=$1::uuid",
            id, enabled, updated_by_user_id);
        return report::TemplateWriteStatus::Ok;
    } catch (...) {
        return report::TemplateWriteStatus::NotFound;
    }
}

report::TemplateWriteStatus ReportTemplateRepository::set_default(
    const std::string& id, const std::string& updated_by_user_id) const {
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = client_->newTransaction(latch->callback());
        const auto existing = tx->execSqlSync(
            "select validation_status from report_templates where id=$1::uuid for update", id);
        if (existing.empty()) {
            tx->rollback();
            return report::TemplateWriteStatus::NotFound;
        }
        if (existing[0]["validation_status"].as<std::string>() != "valid") {
            tx->rollback();
            return report::TemplateWriteStatus::NotValidated;
        }
        // 清旧默认和设新默认必须同一事务：分两步做的话，中间那一刻要么没有默认模板，
        // 要么有两个，都会撞 ux_report_templates_default。
        tx->execSqlSync(
            "update report_templates set is_default=false, updated_at=now() "
            "where is_default and id<>$1::uuid", id);
        // 设为默认意味着"让普通用户用这套"，因此一并启用——默认必然是启用的。
        tx->execSqlSync(
            "update report_templates set is_default=true, is_enabled=true, "
            " updated_by_user_id=$2::uuid, updated_at=now() where id=$1::uuid",
            id, updated_by_user_id);
        tx.reset();
        if (!latch->wait()) return report::TemplateWriteStatus::NotFound;
        return report::TemplateWriteStatus::Ok;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return report::TemplateWriteStatus::NotFound;
    }
}

report::TemplateWriteStatus ReportTemplateRepository::remove(
    const std::string& id,
    std::optional<std::string>& obsolete_storage_relative_path) const {
    obsolete_storage_relative_path.reset();
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = client_->newTransaction(latch->callback());
        const auto existing = tx->execSqlSync(
            "select t.is_default, t.file_id::text as file_id, af.storage_relative_path, "
            "(select count(*) from inspection_report_settings s where s.template_id=t.id) as used "
            "from report_templates t join archived_files af on af.id=t.file_id "
            "where t.id=$1::uuid for update of t",
            id);
        if (existing.empty()) {
            tx->rollback();
            return report::TemplateWriteStatus::NotFound;
        }
        if (existing[0]["used"].as<int>() > 0) {
            tx->rollback();
            return report::TemplateWriteStatus::Referenced;
        }
        if (existing[0]["is_default"].as<bool>()) {
            tx->rollback();
            return report::TemplateWriteStatus::IsDefaultTemplate;
        }
        const auto file_id = existing[0]["file_id"].as<std::string>();
        const auto path = existing[0]["storage_relative_path"].as<std::string>();

        tx->execSqlSync("delete from report_templates where id=$1::uuid", id);
        if (!file_still_referenced(tx, file_id, id)) {
            tx->execSqlSync("delete from archived_files where id=$1::uuid", file_id);
            obsolete_storage_relative_path = path;
        }
        tx.reset();
        if (!latch->wait()) {
            obsolete_storage_relative_path.reset();
            return report::TemplateWriteStatus::NotFound;
        }
        return report::TemplateWriteStatus::Ok;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        obsolete_storage_relative_path.reset();
        return report::TemplateWriteStatus::NotFound;
    }
}

}  // namespace bridge_report::db
