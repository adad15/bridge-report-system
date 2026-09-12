#include "bridge_report/db/InspectionReportSettingsRepository.hpp"

#include <utility>

#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::optional<std::string> optional_text(const drogon::orm::Row& row, const char* column) {
    const auto field = row[column];
    if (field.isNull()) return std::nullopt;
    return field.as<std::string>();
}

/// §12.1 的候选条件，读与写共用同一段谓词：同桥、当前修订、已确认或已归档、
/// 年度早于本年度、且不是本记录自身。写死两遍迟早会漂移。
constexpr const char* kComparisonCandidatePredicate =
    " other.bridge_id = mine.bridge_id "
    " and other.is_current "
    " and other.status in ('已确认', '已归档') "
    " and other.inspection_year < mine.inspection_year "
    " and other.id <> mine.id ";

}  // namespace

InspectionReportSettingsRepository::InspectionReportSettingsRepository(
    drogon::orm::DbClientPtr client)
    : client_(std::move(client)) {}

std::optional<report::InspectionReportSettings>
InspectionReportSettingsRepository::find(const std::string& inspection_year_id) const {
    // 对比选择可能因为对方被修订或状态变化而失效，读的时候就要判出来。用子查询复用
    // 同一份候选谓词（它绑定 mine/other 别名），避免在这里抄第二份条件。
    const auto year_rows = client_->execSqlSync(
        std::string(
            "select iy.id::text as id, iy.inspection_year, "
            " iy.report_comparison_inspection_id::text as comparison_id, "
            " cmp.inspection_year as comparison_year, "
            " exists(select 1 from inspection_years mine join inspection_years other on ")
            + kComparisonCandidatePredicate +
            " where mine.id = iy.id and other.id = iy.report_comparison_inspection_id"
            " ) as comparison_usable "
            "from inspection_years iy "
            "left join inspection_years cmp on cmp.id = iy.report_comparison_inspection_id "
            "where iy.id=$1::uuid",
        inspection_year_id);
    if (year_rows.empty()) return std::nullopt;

    report::InspectionReportSettings settings;
    settings.inspection_year_id = year_rows[0]["id"].as<std::string>();
    settings.inspection_year = year_rows[0]["inspection_year"].as<int>();
    settings.comparison_inspection_id = optional_text(year_rows[0], "comparison_id");
    if (!year_rows[0]["comparison_year"].isNull()) {
        settings.comparison_year = year_rows[0]["comparison_year"].as<int>();
    }
    settings.comparison_is_usable = year_rows[0]["comparison_usable"].as<bool>();

    const auto setting_rows = client_->execSqlSync(
        "select s.template_id::text as template_id, t.template_name, "
        " (t.id is not null and t.is_enabled and t.validation_status='valid') as template_usable, "
        " u.display_name as configured_by, s.configured_at::text as configured_at "
        "from inspection_report_settings s "
        "left join report_templates t on t.id = s.template_id "
        "left join users u on u.id = s.configured_by_user_id "
        "where s.inspection_year_id=$1::uuid",
        inspection_year_id);
    if (!setting_rows.empty()) {
        settings.template_id = optional_text(setting_rows[0], "template_id");
        settings.template_name = optional_text(setting_rows[0], "template_name");
        settings.template_is_usable = setting_rows[0]["template_usable"].as<bool>();
        settings.configured_by_display_name = optional_text(setting_rows[0], "configured_by");
        settings.configured_at = optional_text(setting_rows[0], "configured_at");
    }

    for (const auto& row : client_->execSqlSync(
             "select a.personnel_id::text as personnel_id, p.full_name, p.organization, "
             " p.professional_title, a.role_code, a.sort_order, p.is_enabled "
             "from inspection_report_personnel a "
             "join report_personnel p on p.id=a.personnel_id "
             "where a.inspection_year_id=$1::uuid "
             "order by a.role_code, a.sort_order, p.full_name",
             inspection_year_id)) {
        report::PersonnelAssignment item;
        item.personnel_id = row["personnel_id"].as<std::string>();
        item.full_name = row["full_name"].as<std::string>();
        item.organization = optional_text(row, "organization");
        item.professional_title = optional_text(row, "professional_title");
        item.role_code = row["role_code"].as<std::string>();
        item.sort_order = row["sort_order"].as<int>();
        item.is_enabled = row["is_enabled"].as<bool>();
        settings.personnel.push_back(std::move(item));
    }

    for (const auto& row : client_->execSqlSync(
             "select a.equipment_id::text as equipment_id, e.equipment_name, e.model_spec, "
             " a.purpose, a.sort_order, e.is_enabled, "
             " (e.calibration_valid_until is not null and e.calibration_valid_until < current_date)"
             "   as calibration_expired "
             "from inspection_report_equipment a "
             "join report_equipment e on e.id=a.equipment_id "
             "where a.inspection_year_id=$1::uuid "
             "order by a.sort_order, e.equipment_name",
             inspection_year_id)) {
        report::EquipmentAssignment item;
        item.equipment_id = row["equipment_id"].as<std::string>();
        item.equipment_name = row["equipment_name"].as<std::string>();
        item.model_spec = optional_text(row, "model_spec");
        item.purpose = optional_text(row, "purpose");
        item.sort_order = row["sort_order"].as<int>();
        item.is_enabled = row["is_enabled"].as<bool>();
        item.calibration_expired = row["calibration_expired"].as<bool>();
        settings.equipment.push_back(std::move(item));
    }
    return settings;
}

std::vector<report::ComparisonCandidate>
InspectionReportSettingsRepository::list_comparison_candidates(
    const std::string& inspection_year_id) const {
    std::vector<report::ComparisonCandidate> candidates;
    const auto rows = client_->execSqlSync(
        std::string(
            "select other.id::text as id, other.inspection_year, other.status, "
            " other.report_number, other.overall_grade "
            "from inspection_years mine join inspection_years other on ")
            + kComparisonCandidatePredicate +
            "where mine.id=$1::uuid order by other.inspection_year desc",
        inspection_year_id);
    for (const auto& row : rows) {
        report::ComparisonCandidate candidate;
        candidate.inspection_year_id = row["id"].as<std::string>();
        candidate.inspection_year = row["inspection_year"].as<int>();
        candidate.status = row["status"].as<std::string>();
        candidate.report_number = optional_text(row, "report_number");
        candidate.overall_grade = optional_text(row, "overall_grade");
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

report::SettingsWriteStatus InspectionReportSettingsRepository::save(
    const std::string& inspection_year_id,
    const report::InspectionReportSettingsInput& input) const {
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = client_->newTransaction(latch->callback());

        const auto year = tx->execSqlSync(
            "select id from inspection_years where id=$1::uuid for update", inspection_year_id);
        if (year.empty()) {
            tx->rollback();
            return report::SettingsWriteStatus::YearNotFound;
        }

        if (input.template_id.has_value()) {
            const auto rows = tx->execSqlSync(
                "select (is_enabled and validation_status='valid') as usable "
                "from report_templates where id=$1::uuid",
                *input.template_id);
            if (rows.empty()) {
                tx->rollback();
                return report::SettingsWriteStatus::TemplateNotFound;
            }
            if (!rows[0]["usable"].as<bool>()) {
                tx->rollback();
                return report::SettingsWriteStatus::TemplateNotUsable;
            }
        }

        if (input.comparison_inspection_id.has_value()) {
            // 保存时按 §12.1 的条件重新校验一次：候选列表是前端上一次拿到的快照，
            // 期间对方可能被修订或改了状态。
            const auto rows = tx->execSqlSync(
                std::string(
                    "select 1 from inspection_years mine join inspection_years other on ")
                    + kComparisonCandidatePredicate +
                    "where mine.id=$1::uuid and other.id=$2::uuid",
                inspection_year_id, *input.comparison_inspection_id);
            if (rows.empty()) {
                tx->rollback();
                return report::SettingsWriteStatus::ComparisonInvalid;
            }
        }

        for (const auto& person : input.personnel) {
            const auto rows = tx->execSqlSync(
                "select is_enabled from report_personnel where id=$1::uuid", person.personnel_id);
            if (rows.empty()) {
                tx->rollback();
                return report::SettingsWriteStatus::PersonnelNotFound;
            }
            // 停用的人不能被新配置选中；已经在配置里的历史项由读接口标记后人工处理。
            if (!rows[0]["is_enabled"].as<bool>()) {
                tx->rollback();
                return report::SettingsWriteStatus::PersonnelDisabled;
            }
        }
        for (const auto& item : input.equipment) {
            const auto rows = tx->execSqlSync(
                "select is_enabled from report_equipment where id=$1::uuid", item.equipment_id);
            if (rows.empty()) {
                tx->rollback();
                return report::SettingsWriteStatus::EquipmentNotFound;
            }
            if (!rows[0]["is_enabled"].as<bool>()) {
                tx->rollback();
                return report::SettingsWriteStatus::EquipmentDisabled;
            }
        }

        tx->execSqlSync(
            "insert into inspection_report_settings "
            "(inspection_year_id, template_id, configured_by_user_id, configured_at) "
            "values ($1::uuid, nullif($2,'')::uuid, nullif($3,'')::uuid, now()) "
            "on conflict (inspection_year_id) do update set "
            " template_id=excluded.template_id, "
            " configured_by_user_id=excluded.configured_by_user_id, "
            " configured_at=excluded.configured_at, updated_at=now()",
            inspection_year_id, input.template_id.value_or(""), input.configured_by_user_id);

        // 对比选择的真源是年度行本身，不在配置表里再存一份（设计 §15.3）。
        tx->execSqlSync(
            "update inspection_years set report_comparison_inspection_id=nullif($2,'')::uuid, "
            " updated_at=now() where id=$1::uuid",
            inspection_year_id, input.comparison_inspection_id.value_or(""));

        // 人员和设备是全量覆盖：增量合并会让"取消某个角色"变得没法表达。
        tx->execSqlSync(
            "delete from inspection_report_personnel where inspection_year_id=$1::uuid",
            inspection_year_id);
        for (const auto& person : input.personnel) {
            tx->execSqlSync(
                "insert into inspection_report_personnel "
                "(inspection_year_id, personnel_id, role_code, sort_order) "
                "values ($1::uuid, $2::uuid, $3, $4)",
                inspection_year_id, person.personnel_id, person.role_code, person.sort_order);
        }
        tx->execSqlSync(
            "delete from inspection_report_equipment where inspection_year_id=$1::uuid",
            inspection_year_id);
        for (const auto& item : input.equipment) {
            tx->execSqlSync(
                "insert into inspection_report_equipment "
                "(inspection_year_id, equipment_id, purpose, sort_order) "
                "values ($1::uuid, $2::uuid, nullif($3,''), $4)",
                inspection_year_id, item.equipment_id, item.purpose.value_or(""),
                item.sort_order);
        }

        tx.reset();
        if (!latch->wait()) return report::SettingsWriteStatus::Failed;
        return report::SettingsWriteStatus::Ok;
    } catch (...) {
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return report::SettingsWriteStatus::Failed;
    }
}

}  // namespace bridge_report::db
