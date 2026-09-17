#include "bridge_report/db/ReportContextRepository.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>
#include <json/json.h>
#include <trantor/utils/Logger.h>

#include "bridge_report/report/ReportPreflightModels.hpp"
#include "bridge_report/standards/ComponentWeightTable.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::optional<std::string> optional_text(const drogon::orm::Row& row, const char* column) {
    const auto field = row[column];
    if (field.isNull()) return std::nullopt;
    return field.as<std::string>();
}

std::optional<int> optional_int(const drogon::orm::Row& row, const char* column) {
    const auto field = row[column];
    if (field.isNull()) return std::nullopt;
    return field.as<int>();
}

std::optional<double> optional_double(const drogon::orm::Row& row, const char* column) {
    const auto field = row[column];
    if (field.isNull()) return std::nullopt;
    return field.as<double>();
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

/// 把 "照片2.1-{n}" 里的 {n} 换成序号。模板没配这个部位的格式时退回纯序号——
/// 生成前检查已经拦住这种模板，这里只是不让缺配置变成崩溃。
std::string apply_number_format(const std::string& format, int index) {
    const std::string token = "{n}";
    const auto position = format.find(token);
    if (position == std::string::npos) return std::to_string(index);
    return format.substr(0, position) + std::to_string(index) +
           format.substr(position + token.size());
}

std::string number_format_for(const Json::Value& config, const std::string& part_code) {
    const auto& formats = config["table_number_formats"];
    if (!formats.isObject()) return {};
    const auto key = "DEFECT_PHOTOS:" + part_code;
    return formats[key].isString() ? formats[key].asString() : std::string{};
}

/// §12.2 的来源病害计数键：同一条来源病害拆出的观测共享 source_candidate_id，
/// 只计一次；未拆分的观测没有 range_split_origin，各计一次。
constexpr const char* kSourceDefectKey =
    "coalesce(o.source_import_record_id::text, o.inspection_year_id::text) || ':' || "
    "coalesce(o.source_raw_cells_json->'range_split_origin'->>'source_candidate_id', o.id::text)";

/// §10.2 的结构部位顺序。写成 SQL 表达式而不是在 C++ 里排，是为了让"部位 -> 构件 ->
/// 病害"三级顺序在一条 order by 里一次定死。
constexpr const char* kPartOrder =
    "case o.structure_part when '上部结构' then 1 when '下部结构' then 2 "
    "when '桥面系' then 3 when '全桥' then 4 else 5 end";

/// 评定表里的结构部位是英文代码，病害表里是中文字面值——两套写法必须在一处对齐，
/// 否则第 2 章的部位小节和第 4 章的评定行会各说各话。
std::pair<std::string, std::string> assessment_part(const std::string& value) {
    if (value == "superstructure") return {"SUPERSTRUCTURE", "上部结构"};
    if (value == "substructure") return {"SUBSTRUCTURE", "下部结构"};
    if (value == "deck_system") return {"DECK", "桥面系"};
    if (value == "overall") return {"WHOLE_BRIDGE", "全桥"};
    return {"OTHER", "其他"};
}

/// 结构部位在报告里的输出顺序，与 §10.2 的病害表顺序一致。
int part_rank(const std::string& code) {
    if (code == "SUPERSTRUCTURE") return 1;
    if (code == "SUBSTRUCTURE") return 2;
    if (code == "DECK") return 3;
    if (code == "WHOLE_BRIDGE") return 4;
    return 5;
}

/// 构件评分分档：同一部件类别下按分数聚合，报告的 表4.1-2 就是这么排的。
void load_score_bands(const TransactionPtr& tx, const std::string& run_id,
                      report::ReportAssessment& assessment) {
    std::map<std::string, std::vector<report::ReportScoreBand>> bands;
    for (const auto& row : tx->execSqlSync(
             "select standard_component_category_id as category_id, score, count(*) as n "
             "from assessment_component_results where assessment_run_id=$1::uuid "
             "group by standard_component_category_id, score "
             // 分数由低到高：正式报告先列最差的那一档。
             "order by standard_component_category_id, score",
             run_id)) {
        report::ReportScoreBand band;
        band.score = row["score"].as<double>();
        band.component_count = row["n"].as<int>();
        bands[row["category_id"].as<std::string>()].push_back(band);
    }
    for (auto& category : assessment.categories) {
        if (const auto found = bands.find(category.category_id); found != bands.end()) {
            category.score_bands = found->second;
        }
    }
}

/// 单项控制指标（H21 4.3）。只读触发了的；一条没有就是"不符合任何一条"。
void load_control_indicators(const TransactionPtr& tx, const std::string& run_id,
                             report::ReportAssessment& assessment) {
    for (const auto& row : tx->execSqlSync(
             "select rule_id, message, grade_after from assessment_control_results "
             "where assessment_run_id=$1::uuid and triggered order by rule_id",
             run_id)) {
        report::ReportControlIndicator item;
        item.rule_id = row["rule_id"].as<std::string>();
        item.message = row["message"].as<std::string>();
        item.grade_after = optional_text(row, "grade_after");
        assessment.triggered_controls.push_back(std::move(item));
    }
}

/// 读当前正式评定的各级结果（设计 §13）。不重跑评定，只读已落库的结论。
void load_assessment(const TransactionPtr& tx, const std::string& run_id,
                     report::ReportAssessment& assessment) {
    for (const auto& row : tx->execSqlSync(
             "select result_level, result_key, structure_part, "
             " standard_component_category_id, score, grade, weight, "
             " result_json::text as result_json "
             "from assessment_part_results where assessment_run_id=$1::uuid "
             "order by result_level, result_key",
             run_id)) {
        const auto level = row["result_level"].as<std::string>();
        const auto [code, label] = assessment_part(row["structure_part"].as<std::string>());
        if (level == "全桥") {
            assessment.overall_score = row["score"].as<double>();
            assessment.overall_grade = optional_text(row, "grade");
        } else if (level == "结构") {
            report::ReportAssessmentPart part;
            part.part_code = code;
            part.part_label = label;
            part.score = row["score"].as<double>();
            part.grade = optional_text(row, "grade");
            part.weight = optional_double(row, "weight");
            assessment.parts.push_back(std::move(part));
        } else if (level == "部件") {
            report::ReportAssessmentCategory category;
            category.part_code = code;
            category.part_label = label;
            category.category_id = row["result_key"].as<std::string>();
            category.score = row["score"].as<double>();
            category.grade = optional_text(row, "grade");
            category.weight = optional_double(row, "weight");
            // 部件名称只在 result_json 里，没有单独成列。
            const auto detail = parse_json_object(row["result_json"].as<std::string>());
            if (detail["component_type_name"].isString()) {
                category.category_name =
                    report::display_component_name(detail["component_type_name"].asString());
            }
            assessment.categories.push_back(std::move(category));
        }
    }

    std::map<std::string, int> counts;
    for (const auto& row : tx->execSqlSync(
             "select standard_component_category_id as category_id, count(*) as n "
             "from assessment_component_results where assessment_run_id=$1::uuid "
             "group by standard_component_category_id",
             run_id)) {
        counts[row["category_id"].as<std::string>()] = row["n"].as<int>();
    }
    for (auto& category : assessment.categories) {
        if (const auto found = counts.find(category.category_id); found != counts.end()) {
            category.component_count = found->second;
        }
    }

    const auto by_part_then_key = [](const auto& left, const auto& right) {
        const auto left_rank = part_rank(left.part_code);
        const auto right_rank = part_rank(right.part_code);
        if (left_rank != right_rank) return left_rank < right_rank;
        return left.part_label < right.part_label;
    };
    std::stable_sort(assessment.parts.begin(), assessment.parts.end(), by_part_then_key);
    std::stable_sort(
        assessment.categories.begin(), assessment.categories.end(),
        [](const report::ReportAssessmentCategory& left,
           const report::ReportAssessmentCategory& right) {
            const auto left_rank = part_rank(left.part_code);
            const auto right_rank = part_rank(right.part_code);
            if (left_rank != right_rank) return left_rank < right_rank;
            return left.category_id < right.category_id;
        });
}

/// 部件权重计算表（表4.1-1）。
///
/// 规范包给出本桥型的完整部件清单和原表权重；评定结果给出实际存在的部件、
/// 其重分配后权重和构件数量。两者对齐后，清单里评定结果没有的那些就是"无此构件"，
/// 它们的权重正是被摊掉的那部分——这张表的意义就在于把这个过程写明白。
void load_component_weights(const standards::StandardPackage& package,
                            const std::string& bridge_type_id,
                            const std::map<std::string, double>& effective_weight,
                            const std::map<std::string, int>& component_count,
                            report::ReportAssessment& assessment) {
    int order = 0;
    for (const auto& entry : standards::component_weight_table(package, bridge_type_id)) {
        report::ReportComponentWeight row;
        const auto [code, label] = assessment_part(standards::to_string(entry.structure_part));
        row.part_code = code;
        row.part_label = label;
        row.order = ++order;
        row.category_id = entry.component_type_id;
        if (!entry.component_type_name.empty()) {
            row.category_name = report::display_component_name(entry.component_type_name);
        }
        row.configured_weight = entry.configured_weight;
        if (const auto found = effective_weight.find(entry.component_type_id);
            found != effective_weight.end()) {
            row.present = true;
            row.effective_weight = found->second;
        }
        if (const auto found = component_count.find(entry.component_type_id);
            found != component_count.end()) {
            row.component_count = found->second;
        }
        assessment.component_weights.push_back(std::move(row));
    }
}

}  // namespace

ReportContextRepository::ReportContextRepository(
    drogon::orm::DbClientPtr client,
    std::shared_ptr<const standards::StandardRegistry> registry)
    : client_(std::move(client)), registry_(std::move(registry)) {}

ReportContextRepository::LockedStandard ReportContextRepository::locked_standard(
    const std::shared_ptr<drogon::orm::Transaction>& tx, const std::string& run_id) const {
    if (registry_ == nullptr) return {};

    // 桥型和所锁规范包都记在评定运行上：报告读评定当时用的那一版，绝不按今天的
    // 规范重新推一遍（设计 §13）。
    const auto run = tx->execSqlSync(
        "select p.standard_id, p.package_version, "
        " r.result_summary_json->'result'->>'bridge_type_id' as bridge_type_id "
        "from assessment_runs r "
        "join standard_packages p on p.id=r.technical_condition_package_id "
        "where r.id=$1::uuid",
        run_id);
    if (run.empty() || run[0]["bridge_type_id"].isNull()) return {};

    const standards::StandardPackageKey key{
        standards::StandardFamily::technical_condition,
        run[0]["standard_id"].as<std::string>(),
        run[0]["package_version"].as<std::string>()};
    return {registry_->find(key), run[0]["bridge_type_id"].as<std::string>()};
}

void ReportContextRepository::load_weight_table(
    const LockedStandard& locked, report::ReportAssessment& assessment) const {
    const auto* package = locked.package;
    if (package == nullptr) return;

    std::map<std::string, double> effective_weight;
    std::map<std::string, int> component_count;
    for (const auto& category : assessment.categories) {
        if (category.weight.has_value()) effective_weight[category.category_id] = *category.weight;
        component_count[category.category_id] = category.component_count;
    }
    load_component_weights(*package, locked.bridge_type_id, effective_weight,
                           component_count, assessment);

    // 评定表的部件顺序跟规范原表走（上部承重构件、上部一般构件、支座……），
    // 不跟类别编号的字母序——读者是拿规范的表来对照看的。
    std::map<std::string, int> rank;
    for (const auto& row : assessment.component_weights) rank[row.category_id] = row.order;
    std::stable_sort(assessment.categories.begin(), assessment.categories.end(),
                     [&rank](const report::ReportAssessmentCategory& left,
                             const report::ReportAssessmentCategory& right) {
                         const auto left_rank = rank.count(left.category_id)
                             ? rank.at(left.category_id) : 1'000'000;
                         const auto right_rank = rank.count(right.category_id)
                             ? rank.at(right.category_id) : 1'000'000;
                         return left_rank < right_rank;
                     });
}

std::optional<report::ReportContext> ReportContextRepository::build(
    const std::string& inspection_year_id) const {
    TransactionPtr tx;
    try {
        tx = client_->newTransaction();
        // 一次生成内部必须一致：快照隔离让下面这一串查询看到同一个时刻的数据，
        // 生成过程中别人改了病害或配置都不会漏进来（设计 §5.4）。
        tx->execSqlSync("set transaction isolation level repeatable read");

        const auto head = tx->execSqlSync(
            "select iy.id::text as id, iy.inspection_year, iy.inspection_date::text as inspection_date, "
            " iy.report_number, iy.project_name, iy.inspection_org, iy.overall_grade, "
            " cmp.inspection_year as comparison_year, "
            " iy.report_comparison_inspection_id::text as comparison_id, "
            " b.bridge_name, b.route_number, b.route_name, b.administrative_region, "
            " b.business_code, b.station_mark, b.bridge_type, b.bridge_scale, "
            " b.skew_angle_deg, b.carriageway_width_m, b.sidewalk_width_m, b.deck_pavement, b.expansion_joint_type, b.expansion_joint_piers, b.bearing_type, b.superstructure_form, b.girders_per_span, b.girder_height_m, b.abutment_form, b.pier_form, b.foundation_form, b.design_load, b.design_org, b.construction_org, b.supervision_org, "
            " b.span_combination, b.bridge_length_m, b.bridge_width_m, b.built_year, "
            " b.maintenance_org, "
            " t.id::text as template_id, t.template_code, "
            " t.contract_config_json::text as contract_config_json, "
            " current_date::text as report_date "
            "from inspection_years iy "
            "join bridges b on b.id=iy.bridge_id "
            "left join inspection_years cmp on cmp.id=iy.report_comparison_inspection_id "
            "left join inspection_report_settings s on s.inspection_year_id=iy.id "
            "left join report_templates t on t.id=s.template_id "
            "where iy.id=$1::uuid",
            inspection_year_id);
        if (head.empty() || head[0]["template_id"].isNull()) {
            tx->rollback();
            return std::nullopt;
        }

        report::ReportContext context;
        context.inspection_year_id = head[0]["id"].as<std::string>();
        context.inspection_year = head[0]["inspection_year"].as<int>();
        context.report_no = head[0]["report_number"].isNull()
            ? std::string{}
            : head[0]["report_number"].as<std::string>();
        context.bridge_name = head[0]["bridge_name"].as<std::string>();
        context.route_code = optional_text(head[0], "route_number");
        context.route_name = optional_text(head[0], "route_name");
        context.administrative_region = optional_text(head[0], "administrative_region");
        context.inspection_date = optional_text(head[0], "inspection_date");
        context.project_name = optional_text(head[0], "project_name");
        context.inspection_org = optional_text(head[0], "inspection_org");
        context.overall_grade = optional_text(head[0], "overall_grade");
        context.comparison_year = optional_int(head[0], "comparison_year");
        // 数据库里没有报告日期这一项，构造上下文时写定一次，全篇共用（设计 §7.2）。
        context.report_date = head[0]["report_date"].as<std::string>();
        context.template_id = head[0]["template_id"].as<std::string>();
        context.template_code = head[0]["template_code"].as<std::string>();
        context.template_config =
            parse_json_object(head[0]["contract_config_json"].as<std::string>());
        const auto comparison_id = optional_text(head[0], "comparison_id");

        // 桥梁概况只放档案里真有的事实，缺的字段后面就不出这一行（设计 §14 第 5 条）。
        context.bridge_profile.business_code = optional_text(head[0], "business_code");
        context.bridge_profile.station_mark = optional_text(head[0], "station_mark");
        context.bridge_profile.bridge_type = optional_text(head[0], "bridge_type");
        context.bridge_profile.bridge_scale = optional_text(head[0], "bridge_scale");
        context.bridge_profile.span_combination = optional_text(head[0], "span_combination");
        context.bridge_profile.bridge_length_m = optional_double(head[0], "bridge_length_m");
        context.bridge_profile.bridge_width_m = optional_double(head[0], "bridge_width_m");
        context.bridge_profile.built_year = optional_int(head[0], "built_year");
        context.bridge_profile.maintenance_org = optional_text(head[0], "maintenance_org");
        context.bridge_profile.skew_angle_deg = optional_double(head[0], "skew_angle_deg");
        context.bridge_profile.carriageway_width_m = optional_double(head[0], "carriageway_width_m");
        context.bridge_profile.sidewalk_width_m = optional_double(head[0], "sidewalk_width_m");
        context.bridge_profile.deck_pavement = optional_text(head[0], "deck_pavement");
        context.bridge_profile.expansion_joint_type = optional_text(head[0], "expansion_joint_type");
        context.bridge_profile.expansion_joint_piers = optional_text(head[0], "expansion_joint_piers");
        context.bridge_profile.bearing_type = optional_text(head[0], "bearing_type");
        context.bridge_profile.superstructure_form = optional_text(head[0], "superstructure_form");
        context.bridge_profile.girders_per_span = optional_int(head[0], "girders_per_span");
        context.bridge_profile.girder_height_m = optional_double(head[0], "girder_height_m");
        context.bridge_profile.abutment_form = optional_text(head[0], "abutment_form");
        context.bridge_profile.pier_form = optional_text(head[0], "pier_form");
        context.bridge_profile.foundation_form = optional_text(head[0], "foundation_form");
        context.bridge_profile.design_load = optional_text(head[0], "design_load");
        context.bridge_profile.design_org = optional_text(head[0], "design_org");
        context.bridge_profile.construction_org = optional_text(head[0], "construction_org");
        context.bridge_profile.supervision_org = optional_text(head[0], "supervision_org");

        // ---- §1.1 的图件 ------------------------------------------------------
        // 图属于桥本身，不属于哪一年，所以按桥取；次序交给 Python 侧按报告契约排。
        const auto media = tx->execSqlSync(
            "select m.slot, f.storage_relative_path from bridge_media m "
            "join archived_files f on f.id=m.archived_file_id "
            "join inspection_years iy on iy.bridge_id=m.bridge_id "
            "where iy.id=$1::uuid order by m.slot",
            inspection_year_id);
        for (const auto& row : media) {
            context.bridge_media.push_back(
                {row["slot"].as<std::string>(), row["storage_relative_path"].as<std::string>()});
        }

        // ---- 当前正式评定（设计 §13）------------------------------------------
        // 一律读当前 is_current 的正式评定，绝不重跑，也绝不读旧 Word 里的评分。
        const auto run = tx->execSqlSync(
            "select id::text as id from assessment_runs "
            "where inspection_year_id=$1::uuid and run_kind='正式' and is_current "
            "  and result_status='成功'",
            inspection_year_id);
        const auto run_id = run.empty()
            ? std::optional<std::string>{}
            : std::optional<std::string>{run[0]["id"].as<std::string>()};
        context.assessment.has_formal_run = run_id.has_value();
        LockedStandard locked;

        // (构件, H21 指标) -> 扣分。H21 按「同一构件同一指标只按最重标度扣一次」计分，
        // 所以扣分是这一组的属性，不是单条病害的；下面装配病害表时只把它记在组内第
        // 一条上，其余写 0（设计 §13）。
        std::map<std::pair<std::string, std::string>, double> deduction_by_indicator;
        std::map<std::string, double> component_score;
        if (run_id.has_value()) {
            for (const auto& row : tx->execSqlSync(
                     "select bridge_component_id::text as component_id, score, "
                     " result_json::text as result_json "
                     "from assessment_component_results where assessment_run_id=$1::uuid",
                     *run_id)) {
                const auto component_id = row["component_id"].as<std::string>();
                component_score[component_id] = row["score"].as<double>();
                const auto detail = parse_json_object(row["result_json"].as<std::string>());
                for (const auto& item : detail["defects"]) {
                    if (!item["defect_indicator_id"].isString()) continue;
                    deduction_by_indicator[{component_id,
                                            item["defect_indicator_id"].asString()}] =
                        item["deduction"].asDouble();
                }
            }
            load_assessment(tx, *run_id, context.assessment);
            load_score_bands(tx, *run_id, context.assessment);
            load_control_indicators(tx, *run_id, context.assessment);
            locked = locked_standard(tx, *run_id);
            load_weight_table(locked, context.assessment);
        }

        // ---- 病害：一条 order by 定死"部位 -> 构件 -> 病害"三级顺序 ----------
        const auto defects = tx->execSqlSync(
            std::string(
                // 构件评分和病害扣分都取自当前正式评定，不取病害行——observations 上
                // 那两个导入遗留列已被迁移 014 删除，正是因为它们来自 Word 而不可信。
                "select o.id::text as id, o.structure_part, o.part_name, "
                " o.bridge_component_id::text as component_id, "
                " o.standard_defect_indicator_id, "
                " m.standard_component_category_id as category_id, "
                " coalesce(e.component_number, o.business_component_code) as component_number, "
                " o.defect_location, o.defect_type, o.defect_description_raw, o.scale "
                "from defect_observations o "
                "join inspection_years iy on iy.id=o.inspection_year_id "
                "left join bridge_component_inventory_entries e "
                "  on e.inventory_revision_id=iy.component_inventory_revision_id "
                " and e.bridge_component_id=o.bridge_component_id "
                // 「部件名称」列要的是 H21 部件泛称（盖梁归桥墩、铰缝归上部一般
                // 构件），来源就是台账已确认的规范映射——与 表4.1-2 的「评价部件」
                // 同源，读者才能顺着病害行找到对应的部件评分。
                "left join bridge_component_standard_mappings m "
                "  on m.inventory_entry_id=e.id and m.confirmation_status='已确认' "
                "where o.inspection_year_id=$1::uuid "
                "order by ") + kPartOrder +
                ", e.sort_order nulls last, e.component_number nulls last, "
                " o.created_at, o.id",
            inspection_year_id);

        // 照片按所属病害分组，组内按入库顺序——顺序必须确定，否则同一份上下文
        // 重复生成会得到不同的图号（设计 §5.5、§11.1）。
        std::map<std::string, std::vector<drogon::orm::Row>> photos_by_defect;
        for (const auto& row : tx->execSqlSync(
                 "select p.id::text as id, p.defect_observation_id::text as defect_id, "
                 " p.photo_number, p.photo_title, p.archived_file_id::text as archived_file_id, "
                 " af.storage_relative_path "
                 "from defect_photos p "
                 "join defect_observations o on o.id=p.defect_observation_id "
                 "join archived_files af on af.id=p.archived_file_id "
                 "where o.inspection_year_id=$1::uuid "
                 "order by p.created_at, p.id",
                 inspection_year_id)) {
            photos_by_defect[row["defect_id"].as<std::string>()].push_back(row);
        }

        std::map<std::string, report::ReportStructurePart> parts;
        std::vector<std::string> part_order;
        std::set<std::pair<std::string, std::string>> counted_indicators;
        for (const auto& row : defects) {
            const auto label = row["structure_part"].as<std::string>();
            const auto code = report::structure_part_code(label).value_or("OTHER");
            if (parts.find(code) == parts.end()) {
                report::ReportStructurePart part;
                part.part_code = code;
                part.part_label = label;
                parts.emplace(code, std::move(part));
                part_order.push_back(code);
            }
            auto& part = parts.at(code);

            report::ReportDefectRow entry;
            entry.row_number = static_cast<int>(part.defect_rows.size()) + 1;
            entry.observation_id = row["id"].as<std::string>();
            // 规范映射优先，取不到才退回入库时的来源文字。实测三个年度 1197 行的
            // part_name 全都等于构件编号——来源软件那一列压根不是部件名称，
            // 拿它当泛称等于把两列印成一样的。
            entry.part_name = optional_text(row, "part_name");
            if (const auto category = optional_text(row, "category_id");
                category.has_value() && locked.package != nullptr) {
                if (auto name = standards::component_type_name(*locked.package, *category);
                    !name.empty()) {
                    entry.part_name = report::display_component_name(name);
                }
            }
            entry.component_number = optional_text(row, "component_number");
            entry.defect_location = optional_text(row, "defect_location");
            entry.defect_type = row["defect_type"].as<std::string>();
            entry.description = row["defect_description_raw"].as<std::string>();
            entry.scale = optional_text(row, "scale");

            const auto component_id = row["component_id"].as<std::string>();
            if (const auto found = component_score.find(component_id);
                found != component_score.end()) {
                entry.component_score = found->second;
            }
            // 扣分记在 (构件, 指标) 组内第一条病害上，其余写 0。同组其余行不是
            // "没有数据"，是"按规范没有额外扣分"——写 0 而不是留空，读者把这一列
            // 加起来才对得上（设计 §13）。
            if (const auto indicator = optional_text(row, "standard_defect_indicator_id");
                indicator.has_value() && context.assessment.has_formal_run) {
                const std::pair<std::string, std::string> key{component_id, *indicator};
                if (const auto found = deduction_by_indicator.find(key);
                    found != deduction_by_indicator.end()) {
                    entry.deduction = counted_indicators.insert(key).second ? found->second : 0.0;
                }
            }

            // 图号在这里现编：跟着病害表行序走，每个部位从 1 起（设计 §11.2）。
            // 表里的「照片编号」列和下面照片的图题取的是同一批号码。
            const auto format = number_format_for(context.template_config, code);
            for (const auto& photo_row : photos_by_defect[entry.observation_id]) {
                report::ReportPhoto photo;
                photo.photo_id = photo_row["id"].as<std::string>();
                photo.archived_file_id = photo_row["archived_file_id"].as<std::string>();
                photo.storage_relative_path =
                    photo_row["storage_relative_path"].as<std::string>();
                photo.title = optional_text(photo_row, "photo_title");
                photo.source_photo_number = optional_text(photo_row, "photo_number");
                photo.report_number =
                    apply_number_format(format, static_cast<int>(part.photos.size()) + 1);
                entry.photo_numbers.push_back(photo.report_number);
                part.photos.push_back(std::move(photo));
            }
            part.defect_rows.push_back(std::move(entry));
        }

        // ---- 主要扣分病害，供第 5 章结论概括（设计 §14 第 3 条）-----------------
        // 从已装配好的病害行里取，不另跑一遍查询：结论里点名的病害必须就是病害表
        // 里的那几行，两处各查各的迟早会对不上。
        for (const auto& code : part_order) {
            for (const auto& entry : parts.at(code).defect_rows) {
                if (!entry.deduction.has_value() || *entry.deduction <= 0.0) continue;
                report::ReportTopDeduction item;
                item.part_code = code;
                item.component_number = entry.component_number;
                item.defect_type = entry.defect_type;
                item.deduction = *entry.deduction;
                context.assessment.top_deductions.push_back(std::move(item));
            }
        }
        // 扣分从大到小；同分保持部位与病害表的原顺序，结果才可重复（设计 §5.5）。
        std::stable_sort(
            context.assessment.top_deductions.begin(),
            context.assessment.top_deductions.end(),
            [](const report::ReportTopDeduction& left, const report::ReportTopDeduction& right) {
                return left.deduction > right.deduction;
            });

        // ---- §12.2 来源病害条数对比 -----------------------------------------
        const auto count_by_part = [&](const std::string& year_id) {
            std::map<std::string, int> counts;
            for (const auto& row : tx->execSqlSync(
                     std::string("select o.structure_part, count(distinct (") +
                         kSourceDefectKey + ")) as n "
                         "from defect_observations o where o.inspection_year_id=$1::uuid "
                         "group by o.structure_part",
                     year_id)) {
                const auto code =
                    report::structure_part_code(row["structure_part"].as<std::string>())
                        .value_or("OTHER");
                counts[code] += row["n"].as<int>();
            }
            return counts;
        };
        const auto current_counts = count_by_part(inspection_year_id);
        std::map<std::string, int> previous_counts;
        if (comparison_id.has_value()) previous_counts = count_by_part(*comparison_id);

        for (const auto& code : part_order) {
            auto& part = parts.at(code);
            part.comparison.has_previous = comparison_id.has_value();
            const auto current = current_counts.count(code) ? current_counts.at(code) : 0;
            const auto previous = previous_counts.count(code) ? previous_counts.at(code) : 0;
            part.comparison.current_source_defect_count = current;
            part.comparison.previous_source_defect_count = previous;
            part.comparison.delta = current - previous;
            context.parts.push_back(std::move(part));
        }

        // 合计按唯一计数键直接汇总，不是把各部位相加——那样会掩盖跨部位的异常
        // （设计 §12.2）。
        const auto total_of = [&](const std::string& year_id) {
            return tx->execSqlSync(
                std::string("select count(distinct (") + kSourceDefectKey + ")) as n "
                    "from defect_observations o where o.inspection_year_id=$1::uuid",
                year_id)[0]["n"].as<int>();
        };
        context.overall_comparison.has_previous = comparison_id.has_value();
        context.overall_comparison.current_source_defect_count = total_of(inspection_year_id);
        context.overall_comparison.previous_source_defect_count =
            comparison_id.has_value() ? total_of(*comparison_id) : 0;
        context.overall_comparison.delta =
            context.overall_comparison.current_source_defect_count -
            context.overall_comparison.previous_source_defect_count;

        // ---- 人员与设备 ------------------------------------------------------
        for (const auto& row : tx->execSqlSync(
                 "select p.full_name, p.organization, p.professional_title, "
                 " p.qualification_certificate_no, a.role_code "
                 "from inspection_report_personnel a join report_personnel p on p.id=a.personnel_id "
                 "where a.inspection_year_id=$1::uuid "
                 "order by a.role_code, a.sort_order, a.id",
                 inspection_year_id)) {
            report::ReportPersonnelEntry entry;
            entry.full_name = row["full_name"].as<std::string>();
            entry.organization = optional_text(row, "organization");
            entry.professional_title = optional_text(row, "professional_title");
            entry.qualification_certificate_no =
                optional_text(row, "qualification_certificate_no");
            entry.role_code = row["role_code"].as<std::string>();
            context.personnel.push_back(std::move(entry));
        }

        for (const auto& row : tx->execSqlSync(
                 "select e.equipment_name, e.model_spec, e.asset_number, e.measurement_range, "
                 " e.accuracy, e.calibration_certificate_no, "
                 " e.calibration_valid_until::text as calibration_valid_until, a.purpose "
                 "from inspection_report_equipment a join report_equipment e on e.id=a.equipment_id "
                 "where a.inspection_year_id=$1::uuid order by a.sort_order, a.id",
                 inspection_year_id)) {
            report::ReportEquipmentEntry entry;
            entry.equipment_name = row["equipment_name"].as<std::string>();
            entry.model_spec = optional_text(row, "model_spec");
            entry.asset_number = optional_text(row, "asset_number");
            entry.measurement_range = optional_text(row, "measurement_range");
            entry.accuracy = optional_text(row, "accuracy");
            entry.calibration_certificate_no = optional_text(row, "calibration_certificate_no");
            entry.calibration_valid_until = optional_text(row, "calibration_valid_until");
            entry.purpose = optional_text(row, "purpose");
            context.equipment.push_back(std::move(entry));
        }

        tx->rollback();  // 只读事务，回滚即释放快照。
        return context;
    } catch (const std::exception& error) {
        LOG_WARN << "ReportContext 组装失败 year=" << inspection_year_id
                 << " detail=" << error.what();
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return std::nullopt;
    } catch (...) {
        LOG_WARN << "ReportContext 组装失败（未知异常）year=" << inspection_year_id;
        if (tx) { try { tx->rollback(); } catch (...) {} }
        return std::nullopt;
    }
}

}  // namespace bridge_report::db
