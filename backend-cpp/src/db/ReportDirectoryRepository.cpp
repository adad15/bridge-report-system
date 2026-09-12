#include "bridge_report/db/ReportDirectoryRepository.hpp"

#include <utility>

#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>

namespace bridge_report::db {
namespace {

std::optional<std::string> optional_text(const drogon::orm::Row& row, const char* column) {
    const auto field = row[column];
    if (field.isNull()) return std::nullopt;
    return field.as<std::string>();
}

/// 可空入参统一走"空串 + nullif"：与仓库其他仓储一致，也顺手把 "  " 这类
/// 只有空白的输入归一化成 null，避免它被当成一个有值的字段。
std::string text_or_empty(const std::optional<std::string>& value) {
    if (!value.has_value()) return {};
    const auto& text = *value;
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

constexpr const char* kPersonnelColumns =
    "p.id::text as id, p.full_name, p.organization, p.job_title, p.professional_title, "
    "p.qualification_certificate_no, p.phone, p.email, p.remarks, p.is_enabled, "
    "p.updated_at::text as updated_at, "
    "(select count(*) from inspection_report_personnel a where a.personnel_id=p.id) as assignments ";

constexpr const char* kEquipmentColumns =
    "e.id::text as id, e.equipment_name, e.model_spec, e.asset_number, e.measurement_range, "
    "e.accuracy, e.calibration_certificate_no, e.calibration_valid_until::text as calibration_valid_until, "
    "e.remarks, e.is_enabled, e.updated_at::text as updated_at, "
    "(select count(*) from inspection_report_equipment a where a.equipment_id=e.id) as assignments ";

report::ReportPersonnel read_personnel(const drogon::orm::Row& row) {
    report::ReportPersonnel person;
    person.id = row["id"].as<std::string>();
    person.full_name = row["full_name"].as<std::string>();
    person.organization = optional_text(row, "organization");
    person.job_title = optional_text(row, "job_title");
    person.professional_title = optional_text(row, "professional_title");
    person.qualification_certificate_no = optional_text(row, "qualification_certificate_no");
    person.phone = optional_text(row, "phone");
    person.email = optional_text(row, "email");
    person.remarks = optional_text(row, "remarks");
    person.is_enabled = row["is_enabled"].as<bool>();
    person.assignment_count = row["assignments"].as<int>();
    person.updated_at = row["updated_at"].as<std::string>();
    return person;
}

report::ReportEquipment read_equipment(const drogon::orm::Row& row) {
    report::ReportEquipment item;
    item.id = row["id"].as<std::string>();
    item.equipment_name = row["equipment_name"].as<std::string>();
    item.model_spec = optional_text(row, "model_spec");
    item.asset_number = optional_text(row, "asset_number");
    item.measurement_range = optional_text(row, "measurement_range");
    item.accuracy = optional_text(row, "accuracy");
    item.calibration_certificate_no = optional_text(row, "calibration_certificate_no");
    item.calibration_valid_until = optional_text(row, "calibration_valid_until");
    item.remarks = optional_text(row, "remarks");
    item.is_enabled = row["is_enabled"].as<bool>();
    item.assignment_count = row["assignments"].as<int>();
    item.updated_at = row["updated_at"].as<std::string>();
    return item;
}

}  // namespace

ReportDirectoryRepository::ReportDirectoryRepository(drogon::orm::DbClientPtr client)
    : client_(std::move(client)) {}

// ---------------------------------------------------------------- 人员库

std::vector<report::ReportPersonnel> ReportDirectoryRepository::list_personnel(
    bool only_enabled) const {
    const std::string sql =
        std::string("select ") + kPersonnelColumns +
        "from report_personnel p " +
        (only_enabled ? "where p.is_enabled " : "") +
        "order by p.is_enabled desc, p.full_name, p.id";
    std::vector<report::ReportPersonnel> people;
    for (const auto& row : client_->execSqlSync(sql)) {
        people.push_back(read_personnel(row));
    }
    return people;
}

std::optional<report::ReportPersonnel> ReportDirectoryRepository::find_personnel(
    const std::string& id) const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kPersonnelColumns +
            "from report_personnel p where p.id=$1::uuid",
        id);
    if (rows.empty()) return std::nullopt;
    return read_personnel(rows[0]);
}

report::ReportPersonnel ReportDirectoryRepository::create_personnel(
    const report::ReportPersonnelInput& input) const {
    const auto rows = client_->execSqlSync(
        "insert into report_personnel "
        "(full_name, organization, job_title, professional_title, "
        " qualification_certificate_no, phone, email, remarks) "
        "values ($1, nullif($2,''), nullif($3,''), nullif($4,''), "
        "        nullif($5,''), nullif($6,''), nullif($7,''), nullif($8,'')) "
        "returning id::text as id",
        input.full_name,
        text_or_empty(input.organization),
        text_or_empty(input.job_title),
        text_or_empty(input.professional_title),
        text_or_empty(input.qualification_certificate_no),
        text_or_empty(input.phone),
        text_or_empty(input.email),
        text_or_empty(input.remarks));
    return *find_personnel(rows[0]["id"].as<std::string>());
}

std::optional<report::ReportPersonnel> ReportDirectoryRepository::update_personnel(
    const std::string& id, const report::ReportPersonnelInput& input) const {
    const auto rows = client_->execSqlSync(
        "update report_personnel set full_name=$2, organization=nullif($3,''), "
        " job_title=nullif($4,''), professional_title=nullif($5,''), "
        " qualification_certificate_no=nullif($6,''), phone=nullif($7,''), "
        " email=nullif($8,''), remarks=nullif($9,''), updated_at=now() "
        "where id=$1::uuid returning id::text as id",
        id,
        input.full_name,
        text_or_empty(input.organization),
        text_or_empty(input.job_title),
        text_or_empty(input.professional_title),
        text_or_empty(input.qualification_certificate_no),
        text_or_empty(input.phone),
        text_or_empty(input.email),
        text_or_empty(input.remarks));
    if (rows.empty()) return std::nullopt;
    return find_personnel(id);
}

std::optional<report::ReportPersonnel> ReportDirectoryRepository::set_personnel_enabled(
    const std::string& id, bool enabled) const {
    const auto rows = client_->execSqlSync(
        "update report_personnel set is_enabled=$2, updated_at=now() "
        "where id=$1::uuid returning id::text as id",
        id, enabled);
    if (rows.empty()) return std::nullopt;
    return find_personnel(id);
}

report::DirectoryDeleteStatus ReportDirectoryRepository::delete_personnel(
    const std::string& id) const {
    // 先看有没有被年度配置引用。让外键 RESTRICT 去挡也能拦住，但上层只会拿到一个
    // 数据库异常，说不出"因为被 3 个年度引用所以只能停用"（设计 §15.3）。
    const auto referenced = client_->execSqlSync(
        "select count(*) as used from inspection_report_personnel where personnel_id=$1::uuid",
        id);
    if (referenced[0]["used"].as<int>() > 0) {
        return report::DirectoryDeleteStatus::Referenced;
    }
    const auto rows = client_->execSqlSync(
        "delete from report_personnel where id=$1::uuid returning id", id);
    return rows.empty() ? report::DirectoryDeleteStatus::NotFound
                        : report::DirectoryDeleteStatus::Deleted;
}

// ---------------------------------------------------------------- 设备库

std::vector<report::ReportEquipment> ReportDirectoryRepository::list_equipment(
    bool only_enabled) const {
    const std::string sql =
        std::string("select ") + kEquipmentColumns +
        "from report_equipment e " +
        (only_enabled ? "where e.is_enabled " : "") +
        "order by e.is_enabled desc, e.equipment_name, e.id";
    std::vector<report::ReportEquipment> items;
    for (const auto& row : client_->execSqlSync(sql)) {
        items.push_back(read_equipment(row));
    }
    return items;
}

std::optional<report::ReportEquipment> ReportDirectoryRepository::find_equipment(
    const std::string& id) const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kEquipmentColumns +
            "from report_equipment e where e.id=$1::uuid",
        id);
    if (rows.empty()) return std::nullopt;
    return read_equipment(rows[0]);
}

report::ReportEquipment ReportDirectoryRepository::create_equipment(
    const report::ReportEquipmentInput& input) const {
    const auto rows = client_->execSqlSync(
        "insert into report_equipment "
        "(equipment_name, model_spec, asset_number, measurement_range, accuracy, "
        " calibration_certificate_no, calibration_valid_until, remarks) "
        "values ($1, nullif($2,''), nullif($3,''), nullif($4,''), nullif($5,''), "
        "        nullif($6,''), nullif($7,'')::date, nullif($8,'')) "
        "returning id::text as id",
        input.equipment_name,
        text_or_empty(input.model_spec),
        text_or_empty(input.asset_number),
        text_or_empty(input.measurement_range),
        text_or_empty(input.accuracy),
        text_or_empty(input.calibration_certificate_no),
        text_or_empty(input.calibration_valid_until),
        text_or_empty(input.remarks));
    return *find_equipment(rows[0]["id"].as<std::string>());
}

std::optional<report::ReportEquipment> ReportDirectoryRepository::update_equipment(
    const std::string& id, const report::ReportEquipmentInput& input) const {
    const auto rows = client_->execSqlSync(
        "update report_equipment set equipment_name=$2, model_spec=nullif($3,''), "
        " asset_number=nullif($4,''), measurement_range=nullif($5,''), "
        " accuracy=nullif($6,''), calibration_certificate_no=nullif($7,''), "
        " calibration_valid_until=nullif($8,'')::date, remarks=nullif($9,''), updated_at=now() "
        "where id=$1::uuid returning id::text as id",
        id,
        input.equipment_name,
        text_or_empty(input.model_spec),
        text_or_empty(input.asset_number),
        text_or_empty(input.measurement_range),
        text_or_empty(input.accuracy),
        text_or_empty(input.calibration_certificate_no),
        text_or_empty(input.calibration_valid_until),
        text_or_empty(input.remarks));
    if (rows.empty()) return std::nullopt;
    return find_equipment(id);
}

std::optional<report::ReportEquipment> ReportDirectoryRepository::set_equipment_enabled(
    const std::string& id, bool enabled) const {
    const auto rows = client_->execSqlSync(
        "update report_equipment set is_enabled=$2, updated_at=now() "
        "where id=$1::uuid returning id::text as id",
        id, enabled);
    if (rows.empty()) return std::nullopt;
    return find_equipment(id);
}

report::DirectoryDeleteStatus ReportDirectoryRepository::delete_equipment(
    const std::string& id) const {
    const auto referenced = client_->execSqlSync(
        "select count(*) as used from inspection_report_equipment where equipment_id=$1::uuid",
        id);
    if (referenced[0]["used"].as<int>() > 0) {
        return report::DirectoryDeleteStatus::Referenced;
    }
    const auto rows = client_->execSqlSync(
        "delete from report_equipment where id=$1::uuid returning id", id);
    return rows.empty() ? report::DirectoryDeleteStatus::NotFound
                        : report::DirectoryDeleteStatus::Deleted;
}

}  // namespace bridge_report::db
