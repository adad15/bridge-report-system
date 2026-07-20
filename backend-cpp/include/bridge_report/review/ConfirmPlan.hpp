#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::review {

/**
 * @brief 构件写入计划：对应 bridge_components 一行（+ 非空 alias_text 时另写 component_aliases 一行）。
 *
 * normalized_component_key 由 structure_part/component_type/business_component_code 三段
 * 各自去首尾空白、内部连续空白压缩为单个空格后以 "|" 连接而成，用于同一构件的去重与
 * DefectPlan::component_key 的关联定位。
 */
struct ComponentPlan {
    std::optional<std::string> existing_bridge_component_id;
    std::string structure_part;
    std::string component_type;
    std::string business_component_code;
    std::string normalized_component_key;
    std::optional<std::string> alias_text;
};

/**
 * @brief 尺寸写入计划：对应 defect_measurements 一行。
 */
struct MeasurementPlan {
    std::string measurement_type;
    std::optional<std::string> value_type;
    std::optional<double> numeric_value;
    std::optional<double> minimum_value;
    std::optional<double> maximum_value;
    std::optional<std::string> unit;
    bool is_approximate{false};
    std::string raw_text;
    bool is_auto_parsed{false};
};

/**
 * @brief 病害写入计划：对应 defect_observations 一行 + 其 measurements 子表。
 *
 * component_key 等于该病害所属 ComponentPlan 的 normalized_component_key，供 Task 8
 * 事务执行器按 (bridge_id, normalized_component_key) upsert 构件后回填外键。
 */
struct DefectPlan {
    std::string candidate_id;
    std::string component_key;
    std::string structure_part;
    std::optional<std::string> part_name;
    std::string defect_location;
    // 规范病害标度：只来自合同 2.0 的 defect_scale（整数转十进制字符串），
    // 严禁取 severity——severity 只是 info/warning/error 校对提示级别。
    std::optional<std::string> scale;
    std::string defect_type;
    std::string defect_description_raw;
    std::optional<std::string> raw_row_text;
    std::optional<std::string> source_table_title;
    std::optional<int> source_table_index;
    std::optional<int> source_row_number;
    double extraction_confidence{0.0};
    std::string review_status;
    std::optional<std::string> review_note;
    std::vector<MeasurementPlan> measurements;
};

/**
 * @brief 照片写入计划：对应 defect_photos 一行。
 */
struct PhotoPlan {
    std::string candidate_id;
    std::string defect_candidate_id;
    std::string photo_number;
    std::optional<std::string> photo_title;
    std::string archive_relative_path;
};

/**
 * @brief 入库写计划：事务执行器的唯一输入，只映射构件、病害和照片正式事实。
 * 技术状况评分由系统评定服务另行生成。
 */
struct ConfirmPlan {
    std::vector<ComponentPlan> components;
    std::vector<DefectPlan> defects;
    std::vector<PhotoPlan> photos;
};

/**
 * @brief 纯函数：把已通过入库前检查的 BridgeAnnualInspectionData 候选 JSON 映射为入库写计划。
 *
 * 假定调用方已跑过 build_preflight_report 且 can_confirm 为 true，但本函数不依赖该前提——
 * 所有字段访问均做防御性判空/判类型，遇到结构异常的条目直接跳过而不是抛异常，
 * 因为 preflight 未覆盖到的极端输入（如脏数据直接调用）也不应导致崩溃。
 *
 * 映射规则详见模块 05 实施计划 Task 7。
 */
[[nodiscard]] ConfirmPlan build_confirm_plan(const Json::Value& data);

}  // 命名空间 bridge_report::review
