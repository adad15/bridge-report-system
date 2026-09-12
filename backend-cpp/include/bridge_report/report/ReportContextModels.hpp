#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::report {

/**
 * @brief 报告里印的部件名称：去掉规范名称中的举例括号。
 *
 * 规范包写的是「上部承重构件（主梁、挂梁）」「上部一般构件（湿接缝、横隔板等）」，
 * 括号里是举例说明，不是名称本身。病害表那一列只有一格宽，15 个字要折三四行，
 * 而上部一般构件一类就占了 200 条——整张病害表会被撑得没法看。
 *
 * 只在报告这一层截，规范包和评定结果里存的仍是全称：这是印刷用词，不是事实变更。
 *
 * 全套 H21 部件里带括号的只有 6 个，截断后彼此不重名；`桥面板`、`索塔`、
 * `横向联结系` 这几组同名的分属不同桥型，一份报告只涉及一种桥型，不会撞上。
 */
std::string display_component_name(const std::string& name);

/// 报告里的一张照片。
struct ReportPhoto {
    std::string photo_id;
    std::string archived_file_id;
    std::string storage_relative_path;
    /// 报告里的图号，生成时按模板结构和病害表行序现编（设计 §7.5、§11.2）。
    std::string report_number;
    std::optional<std::string> title;
    /// 系统内部编号：Word 导入路的匹配键，来源软件导入为空。只作来源证据，不进报告
    /// 正文（设计 §9.1）。放进上下文是为了排障时能对回原始数据。
    std::optional<std::string> source_photo_number;

    /// 图题：「{图号}␠␠{标题}」，标题为空时只输出图号（设计 §11.7）。
    std::string caption() const;
    Json::Value to_json() const;
};

/// 病害表里的一行。
struct ReportDefectRow {
    /// 表内序号，从 1 起。
    int row_number{0};
    std::string observation_id;
    std::optional<std::string> part_name;
    std::optional<std::string> component_number;
    std::optional<std::string> defect_location;
    std::string defect_type;
    std::string description;
    /// 标度。库里是文本列，报告照原样印，不在这里当数字解析——非数字的标度会让
    /// 整次组装失败，而它对报告只是一格文字。
    std::optional<std::string> scale;
    /// 本行病害对构件评分的扣分（设计 §13）。
    ///
    /// H21 按「同一构件、同一指标只按最重标度扣一次」计分，所以扣分是
    /// (构件, 指标) 这一组的属性，不是单条病害的。同组内只有被计入的那条带扣分值，
    /// 其余为 0——不是"没有数据"，是"没有额外扣分"。
    std::optional<double> deduction;
    std::optional<double> component_score;
    /// 「照片编号」列。与图题共用同一套现编号码——两处对不上，读者按表里的号就
    /// 找不到图，这是本方案唯一的致命失败模式（设计 §11.8）。
    std::vector<std::string> photo_numbers;

    Json::Value to_json() const;
};

/// 一个结构部位的对比结论（设计 §12.2）。只比来源病害条数，不表达语义。
struct PartComparison {
    int current_source_defect_count{0};
    int previous_source_defect_count{0};
    int delta{0};
    /// 有可比的历史检查时才有意义。
    bool has_previous{false};

    Json::Value to_json() const;
};

/// 报告第 2 章里的一节：一个结构部位自带病害表、照片和对比（设计 §7.3）。
struct ReportStructurePart {
    std::string part_code;
    std::string part_label;
    std::vector<ReportDefectRow> defect_rows;
    std::vector<ReportPhoto> photos;
    PartComparison comparison;

    Json::Value to_json() const;
};

/// 一个结构部位的评定结果（设计 §13）。
struct ReportAssessmentPart {
    std::string part_code;
    std::string part_label;
    double score{0.0};
    std::optional<std::string> grade;
    /// 该结构在全桥综合评分里的权重。
    std::optional<double> weight;

    Json::Value to_json() const;
};

/// 某个部件类别下、同一构件评分的一档（总体技术状况评定表的一个子行）。
///
/// 正式报告的 表4.1-2 在每个评价部件下按构件评分分档，列出各档的构件数量
/// （如上部承重构件：7 个 65 分、39 个 75 分、779 个 100 分）。
struct ReportScoreBand {
    double score{0.0};
    int component_count{0};

    Json::Value to_json() const;
};

/// 一个部件类别的评定结果，即总体技术状况评定表的一个部件行组（设计 §13）。
struct ReportAssessmentCategory {
    std::string part_code;
    std::string part_label;
    /// 规范里的部件类别编号。
    std::string category_id;
    std::optional<std::string> category_name;
    int component_count{0};
    double score{0.0};
    std::optional<std::string> grade;
    /// 该部件在本结构评分里的权重。
    std::optional<double> weight;
    /// 构件评分分档，按分数从低到高——与正式报告一致，先看最差的。
    std::vector<ReportScoreBand> score_bands;

    Json::Value to_json() const;
};

/// 部件权重计算表（表4.1-1）的一行（设计 §13.1）。
struct ReportComponentWeight {
    std::string part_code;
    std::string part_label;
    /// 全表连续的序号，从 1 起。
    int order{0};
    std::string category_id;
    std::optional<std::string> category_name;
    /// 规范原表里的权重。
    double configured_weight{0.0};
    /// 重分配后的权重；本桥没有这个部件时为空。
    std::optional<double> effective_weight;
    /// 本桥该部件的构件数量；没有这个部件时为空。
    std::optional<int> component_count;
    /// 本桥是否有这个部件。没有时表里注明"无此构件"，其权重摊给同部位其余部件。
    bool present{false};

    Json::Value to_json() const;
};

/// 触发的单项控制指标（设计 §13.2、H21 4.3）。
struct ReportControlIndicator {
    std::string rule_id;
    std::string message;
    std::optional<std::string> grade_after;

    Json::Value to_json() const;
};

/// 主要扣分病害，用于第 5 章结论的概括（设计 §14 第 3 条）。
struct ReportTopDeduction {
    std::string part_code;
    std::optional<std::string> component_number;
    std::string defect_type;
    double deduction{0.0};

    Json::Value to_json() const;
};

/// 当前正式评定的结果（设计 §13）。
///
/// 一律取当前年度 `is_current` 的正式评定，绝不重跑评定，也不读旧 Word 里的评分。
struct ReportAssessment {
    /// 没有当前正式评定时为假；此时第 4 章和结论都不该生成，生成前检查会拦住。
    bool has_formal_run{false};
    std::optional<double> overall_score;
    std::optional<std::string> overall_grade;
    std::vector<ReportAssessmentPart> parts;
    std::vector<ReportAssessmentCategory> categories;
    std::vector<ReportTopDeduction> top_deductions;
    /// 部件权重计算表（表4.1-1）。含本桥没有的部件，注明"无此构件"。
    std::vector<ReportComponentWeight> component_weights;
    /// 触发的单项控制指标；为空即"不符合任何一条，不采用单项控制指标评定"。
    std::vector<ReportControlIndicator> triggered_controls;

    Json::Value to_json() const;
};

/// 桥梁概况：只放 bridges 表里真有的事实，缺的就不出（设计 §14 第 5 条的同一条纪律）。
struct ReportBridgeProfile {
    std::optional<std::string> business_code;
    std::optional<std::string> station_mark;
    std::optional<std::string> bridge_type;
    std::optional<std::string> bridge_scale;
    std::optional<std::string> span_combination;
    std::optional<double> bridge_length_m;
    std::optional<double> bridge_width_m;
    std::optional<int> built_year;
    std::optional<std::string> maintenance_org;

    // ---- §1.1 叙述文字用到的那些事实 ------------------------------------
    //
    // 正式报告的「桥梁概况」是三段话，不是一张两列表。下面每一项都是那几句话里的
    // 一个数或一个词；缺哪一项就少写哪一句，不编数据（设计 §14 第 5 条）。
    std::optional<double> skew_angle_deg;
    /// §1.1 印作「桥面净宽」，附录2 第 24 格印作「行车道宽」，同一个量。
    std::optional<double> carriageway_width_m;
    std::optional<double> sidewalk_width_m;
    std::optional<std::string> deck_pavement;
    std::optional<std::string> expansion_joint_type;
    std::optional<std::string> expansion_joint_piers;
    std::optional<std::string> bearing_type;
    std::optional<std::string> superstructure_form;
    std::optional<int> girders_per_span;
    std::optional<double> girder_height_m;
    std::optional<std::string> abutment_form;
    std::optional<std::string> pier_form;
    std::optional<std::string> foundation_form;
    std::optional<std::string> design_load;
    std::optional<std::string> design_org;
    std::optional<std::string> construction_org;
    std::optional<std::string> supervision_org;

    Json::Value to_json() const;
};

struct ReportPersonnelEntry {
    std::string full_name;
    std::optional<std::string> organization;
    std::optional<std::string> professional_title;
    std::optional<std::string> qualification_certificate_no;
    std::string role_code;

    Json::Value to_json() const;
};

struct ReportEquipmentEntry {
    std::string equipment_name;
    std::optional<std::string> model_spec;
    std::optional<std::string> asset_number;
    std::optional<std::string> measurement_range;
    std::optional<std::string> accuracy;
    std::optional<std::string> calibration_certificate_no;
    std::optional<std::string> calibration_valid_until;
    std::optional<std::string> purpose;

    Json::Value to_json() const;
};

/// 一次生成的临时、只读输入模型（设计 §9.2）。
///
/// 在一次一致性读取里构造，Python 只认它；任务结束或过期后随上下文一并删除
/// （设计 §5.4）。不写入任何永久报告快照表。
struct ReportContext {
    // ---- 标量占位符的取值（设计 §7.2 的闭集） ---------------------------
    std::string inspection_year_id;
    std::string report_no;
    std::string bridge_name;
    std::optional<std::string> route_code;
    std::optional<std::string> route_name;
    std::optional<std::string> administrative_region;
    std::optional<std::string> inspection_date;
    int inspection_year{0};
    std::optional<std::string> project_name;
    std::optional<std::string> inspection_org;
    /// 数据库里没有这一项，构造上下文时一次性写定（设计 §7.2）。
    std::string report_date;
    std::optional<std::string> overall_grade;
    std::optional<int> comparison_year;

    // ---- 内容块 ---------------------------------------------------------
    std::vector<ReportStructurePart> parts;
    std::vector<ReportPersonnelEntry> personnel;
    std::vector<ReportEquipmentEntry> equipment;
    ReportAssessment assessment;
    ReportBridgeProfile bridge_profile;

    /// 全部结构部位合计的对比。按唯一来源计数键直接汇总，不是把各部位相加
    /// （设计 §12.2）。
    PartComparison overall_comparison;

    /// 模板的编号格式与所需角色，即 report_templates.contract_config_json。
    Json::Value template_config;
    std::string template_id;
    std::string template_code;

    Json::Value to_json() const;
};

}  // namespace bridge_report::report
