#include "bridge_report/db/ReportPreflightRepository.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <utility>

#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>
#include <json/json.h>

namespace bridge_report::db {
namespace {

using report::PreflightFinding;
using report::PreflightSeverity;
using report::ReportPreflightResult;

void block(ReportPreflightResult& result, std::string code, std::string message) {
    result.findings.push_back(
        PreflightFinding{std::move(code), std::move(message), PreflightSeverity::Blocking});
}

void warn(ReportPreflightResult& result, std::string code, std::string message) {
    result.findings.push_back(
        PreflightFinding{std::move(code), std::move(message), PreflightSeverity::Warning});
}

Json::Value parse_json(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &value, &errors)) {
        return Json::Value(Json::objectValue);
    }
    return value;
}

/// 从模板的校验结论里取出"按部位拆分的锚点覆盖了哪些结构部位"。
///
/// 锚点顺序是模板校验时存下来的（validation_result_json.anchors_in_document_order），
/// 形如 DEFECT_TABLES:SUPERSTRUCTURE。这里只取冒号后半段。
std::set<std::string> covered_parts(const Json::Value& validation_result) {
    std::set<std::string> parts;
    const auto& anchors = validation_result["anchors_in_document_order"];
    if (!anchors.isArray()) return parts;
    for (const auto& anchor : anchors) {
        if (!anchor.isString()) continue;
        const auto text = anchor.asString();
        const auto colon = text.find(':');
        if (colon != std::string::npos && colon + 1 < text.size()) {
            parts.insert(text.substr(colon + 1));
        }
    }
    return parts;
}

std::string join(const std::vector<std::string>& items) {
    std::string joined;
    for (const auto& item : items) {
        if (!joined.empty()) joined += "、";
        joined += item;
    }
    return joined;
}

}  // namespace

ReportPreflightRepository::ReportPreflightRepository(drogon::orm::DbClientPtr client)
    : client_(std::move(client)) {}

std::optional<ReportPreflightResult> ReportPreflightRepository::evaluate(
    const std::string& inspection_year_id) const {
    const auto year_rows = client_->execSqlSync(
        "select iy.id::text as id, iy.inspection_year, iy.status, iy.is_current, "
        " iy.overall_grade, iy.bridge_id::text as bridge_id, "
        " iy.component_inventory_revision_id::text as inventory_revision_id, "
        " iy.standard_profile_id::text as standard_profile_id, "
        " iy.report_comparison_inspection_id::text as comparison_id "
        "from inspection_years iy where iy.id=$1::uuid",
        inspection_year_id);
    if (year_rows.empty()) return std::nullopt;

    ReportPreflightResult result;
    result.inspection_year_id = year_rows[0]["id"].as<std::string>();
    result.inspection_year = year_rows[0]["inspection_year"].as<int>();
    if (!year_rows[0]["overall_grade"].isNull()) {
        result.overall_grade = year_rows[0]["overall_grade"].as<std::string>();
    }
    const auto status = year_rows[0]["status"].as<std::string>();
    const auto bridge_id = year_rows[0]["bridge_id"].as<std::string>();

    // ---- §16.1 年度检查数据已经正式确认 ---------------------------------
    if (status != "已确认" && status != "已归档") {
        block(result, "REPORT_FORMAL_DATA_REQUIRED",
              "该年度检查尚未正式确认（当前状态：" + status + "），没有可用于报告的正式数据。");
    }
    if (!year_rows[0]["is_current"].as<bool>()) {
        block(result, "REPORT_FORMAL_DATA_REQUIRED",
              "该年度记录已被修订，不是当前有效版本，不能作为报告依据。");
    }

    // ---- 内容摘要与结构部位 ---------------------------------------------
    const auto counts = client_->execSqlSync(
        "select count(*) as observations, "
        " count(distinct o.bridge_component_id) as components, "
        // 去重后的来源病害数：同一条来源病害拆分出的观测共享 source_candidate_id，
        // 只计一次；未拆分的观测没有 range_split_origin，各计一次（设计 §12.2）。
        " count(distinct ("
        "   coalesce(o.source_import_record_id::text, o.inspection_year_id::text) || ':' || "
        "   coalesce(o.source_raw_cells_json->'range_split_origin'->>'source_candidate_id', o.id::text)"
        " )) as source_defects "
        "from defect_observations o where o.inspection_year_id=$1::uuid",
        inspection_year_id);
    result.defect_observation_count = counts[0]["observations"].as<int>();
    result.defect_component_count = counts[0]["components"].as<int>();
    result.source_defect_count = counts[0]["source_defects"].as<int>();

    const auto photo_counts = client_->execSqlSync(
        "select count(*) as total, count(*) filter (where p.archived_file_id is null) as orphaned "
        "from defect_photos p join defect_observations o on o.id=p.defect_observation_id "
        "where o.inspection_year_id=$1::uuid",
        inspection_year_id);
    result.photo_count = photo_counts[0]["total"].as<int>();

    // ---- §16.8 照片必须都有归档文件（磁盘可读性由调用方补） -------------
    if (photo_counts[0]["orphaned"].as<int>() > 0) {
        block(result, "REPORT_PHOTO_UNREADABLE",
              "有 " + std::to_string(photo_counts[0]["orphaned"].as<int>()) +
                  " 张正式照片没有对应的归档文件，无法插入报告。");
    }

    for (const auto& row : client_->execSqlSync(
             "select distinct o.structure_part from defect_observations o "
             "where o.inspection_year_id=$1::uuid order by o.structure_part",
             inspection_year_id)) {
        const auto label = row["structure_part"].as<std::string>();
        if (const auto code = report::structure_part_code(label)) {
            result.structure_parts_with_data.push_back(*code);
        }
    }

    // ---- §10.1 正式事实的来源必须唯一 -----------------------------------
    const auto sources = client_->execSqlSync(
        "select count(distinct o.source_import_record_id) as sources "
        "from defect_observations o "
        "where o.inspection_year_id=$1::uuid and o.source_import_record_id is not null",
        inspection_year_id);
    if (sources[0]["sources"].as<int>() > 1) {
        block(result, "REPORT_FORMAL_SOURCE_AMBIGUOUS",
              "该年度修订内存在多个正式来源导入记录，不能把多次导入拼成一份报告。"
              "这属于数据不变量损坏，请先排查。");
    }

    // ---- §16.2 病害必须绑在锁定台账里的在用构件上 -----------------------
    const auto binding = client_->execSqlSync(
        "select "
        // 台账里没有这个构件：绑定指向了锁定修订版之外的东西。
        " count(*) filter (where e.id is null) as unbound, "
        " count(*) filter (where e.id is not null and not e.is_active) as inactive "
        "from defect_observations o "
        "join inspection_years iy on iy.id=o.inspection_year_id "
        "left join bridge_component_inventory_entries e "
        "  on e.inventory_revision_id = iy.component_inventory_revision_id "
        " and e.bridge_component_id = o.bridge_component_id "
        "where o.inspection_year_id=$1::uuid",
        inspection_year_id);
    if (binding[0]["unbound"].as<int>() > 0) {
        block(result, "REPORT_FORMAL_DATA_REQUIRED",
              "有 " + std::to_string(binding[0]["unbound"].as<int>()) +
                  " 条正式病害绑定的构件不在本年度锁定的构件档案修订版内。");
    }
    if (binding[0]["inactive"].as<int>() > 0) {
        block(result, "REPORT_COMPONENT_INACTIVE",
              "有 " + std::to_string(binding[0]["inactive"].as<int>()) +
                  " 条正式病害绑在已停用的构件上。停用构件不应带有本年度病害，请先核对。");
    }

    // ---- §13 / §16.3 当前正式评定唯一且与年度锁定的版本一致 -------------
    const auto runs = client_->execSqlSync(
        "select count(*) as current_runs, "
        " count(*) filter (where r.component_inventory_revision_id is distinct from "
        "   iy.component_inventory_revision_id) as inventory_mismatch, "
        " count(*) filter (where r.standard_profile_id is distinct from iy.standard_profile_id) "
        "   as profile_mismatch "
        "from assessment_runs r join inspection_years iy on iy.id=r.inspection_year_id "
        "where r.inspection_year_id=$1::uuid and r.run_kind='正式' and r.is_current",
        inspection_year_id);
    const auto current_runs = runs[0]["current_runs"].as<int>();
    if (current_runs == 0) {
        block(result, "REPORT_ASSESSMENT_REQUIRED",
              "该年度没有当前有效的正式技术状况评定，报告的评定结果和附录无从生成。");
    } else {
        // 唯一性由 ux_assessment_runs_current_formal 保证；这里只可能是 1。
        if (runs[0]["inventory_mismatch"].as<int>() > 0) {
            block(result, "REPORT_ASSESSMENT_STALE",
                  "当前正式评定锁定的构件档案修订版与年度记录不一致，请重新评定。");
        }
        if (runs[0]["profile_mismatch"].as<int>() > 0) {
            block(result, "REPORT_ASSESSMENT_STALE",
                  "当前正式评定使用的标准配置与年度记录不一致，请重新评定。");
        }
    }

    // ---- §16.4 模板已选、已启用、校验有效 -------------------------------
    const auto template_rows = client_->execSqlSync(
        "select t.id::text as id, t.template_name, t.is_enabled, t.validation_status, "
        " t.contract_config_json::text as contract_config_json, "
        " t.validation_result_json::text as validation_result_json "
        "from inspection_report_settings s join report_templates t on t.id=s.template_id "
        "where s.inspection_year_id=$1::uuid",
        inspection_year_id);
    Json::Value contract_config(Json::objectValue);
    std::set<std::string> template_parts;
    if (template_rows.empty()) {
        block(result, "REPORT_TEMPLATE_INVALID", "尚未为本年度选择报告模板。");
    } else {
        contract_config = parse_json(template_rows[0]["contract_config_json"].as<std::string>());
        const auto validation =
            parse_json(template_rows[0]["validation_result_json"].as<std::string>());
        template_parts = covered_parts(validation);
        result.template_covered_parts.assign(template_parts.begin(), template_parts.end());

        if (!template_rows[0]["is_enabled"].as<bool>() ||
            template_rows[0]["validation_status"].as<std::string>() != "valid") {
            block(result, "REPORT_TEMPLATE_INVALID",
                  "所选模板「" + template_rows[0]["template_name"].as<std::string>() +
                      "」已停用或未通过契约校验，请重新选择。");
        }
    }

    // ---- §16.10 数据涉及的每个结构部位，模板都要有对应锚点 --------------
    if (!template_parts.empty()) {
        std::vector<std::string> missing;
        for (const auto& part : result.structure_parts_with_data) {
            if (template_parts.count(part) == 0) missing.push_back(part);
        }
        if (!missing.empty()) {
            block(result, "REPORT_STRUCTURE_PART_NOT_IN_TEMPLATE",
                  "本次数据含结构部位 " + join(missing) +
                      " 的正式病害，但所选模板没有对应的内容锚点。"
                      "这些病害将无处输出，请换用覆盖该部位的模板。");
        }
    }

    // ---- §16.5 模板要求的人员角色都已配置 -------------------------------
    const auto& required_roles = contract_config["required_personnel_roles"];
    if (required_roles.isArray() && !required_roles.empty()) {
        std::set<std::string> assigned;
        for (const auto& row : client_->execSqlSync(
                 "select distinct role_code from inspection_report_personnel "
                 "where inspection_year_id=$1::uuid",
                 inspection_year_id)) {
            assigned.insert(row["role_code"].as<std::string>());
        }
        std::vector<std::string> missing;
        for (const auto& role : required_roles) {
            if (role.isString() && assigned.count(role.asString()) == 0) {
                missing.push_back(role.asString());
            }
        }
        if (!missing.empty()) {
            block(result, "REPORT_PERSONNEL_REQUIRED",
                  "模板要求的人员角色尚未配置：" + join(missing) + "。");
        }
    }

    // 已停用的人员和设备仍挂在配置上：不能静默沿用（设计 §15.3）。
    const auto disabled = client_->execSqlSync(
        "select "
        " (select count(*) from inspection_report_personnel a join report_personnel p "
        "   on p.id=a.personnel_id where a.inspection_year_id=$1::uuid and not p.is_enabled) as people, "
        " (select count(*) from inspection_report_equipment a join report_equipment e "
        "   on e.id=a.equipment_id where a.inspection_year_id=$1::uuid and not e.is_enabled) as gear",
        inspection_year_id);
    if (disabled[0]["people"].as<int>() > 0) {
        block(result, "REPORT_PERSONNEL_REQUIRED",
              "报告配置中有已停用的人员，请重新确认或替换后再生成。");
    }
    if (disabled[0]["gear"].as<int>() > 0) {
        block(result, "REPORT_EQUIPMENT_REQUIRED",
              "报告配置中有已停用的设备，请重新确认或替换后再生成。");
    }

    // ---- §16.6 检测设备已配置 -------------------------------------------
    const auto equipment = client_->execSqlSync(
        "select count(*) as n from inspection_report_equipment where inspection_year_id=$1::uuid",
        inspection_year_id);
    if (equipment[0]["n"].as<int>() == 0) {
        block(result, "REPORT_EQUIPMENT_REQUIRED", "尚未配置本次检测使用的设备。");
    }

    // ---- §16.7 有历史正式检查时必须选对比 -------------------------------
    const auto comparison = client_->execSqlSync(
        "select "
        " (select count(*) from inspection_years other where other.bridge_id=$2::uuid "
        "   and other.is_current and other.status in ('已确认','已归档') "
        "   and other.inspection_year < $3 and other.id <> $1::uuid) as candidates, "
        " (select count(*) from inspection_years mine join inspection_years other "
        "   on other.bridge_id=mine.bridge_id and other.is_current "
        "  and other.status in ('已确认','已归档') "
        "  and other.inspection_year < mine.inspection_year and other.id <> mine.id "
        "  where mine.id=$1::uuid and other.id=mine.report_comparison_inspection_id) as selected_ok",
        inspection_year_id, bridge_id, result.inspection_year);
    const auto candidate_count = comparison[0]["candidates"].as<int>();
    const auto selected_ok = comparison[0]["selected_ok"].as<int>() > 0;
    const bool has_selection = !year_rows[0]["comparison_id"].isNull();
    if (candidate_count > 0 && !has_selection) {
        block(result, "REPORT_COMPARISON_REQUIRED",
              "本桥存在可对比的历史正式检查，生成前必须先选择一条。");
    } else if (has_selection && !selected_ok) {
        block(result, "REPORT_COMPARISON_SOURCE_INVALID",
              "所选历史对比检查已失效（可能已被修订或改变状态），请重新选择。");
    } else if (candidate_count == 0) {
        // 不阻断：无历史可比时报告输出固定说明（设计 §12.1）。
        warn(result, "REPORT_COMPARISON_REQUIRED",
             "本桥暂无可供对比的历史正式检查，报告将输出「暂无可供对比的历史正式检查记录」。");
    }

    return result;
}

}  // namespace bridge_report::db
