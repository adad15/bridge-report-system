#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::report {

/// 结构部位代码与数据库字面值的对应（设计 §7.3）。顺序即报告输出顺序。
struct StructurePartCode {
    const char* code;
    const char* label;
};

inline constexpr StructurePartCode kStructureParts[] = {
    {"SUPERSTRUCTURE", "上部结构"},
    {"SUBSTRUCTURE", "下部结构"},
    {"DECK", "桥面系"},
    {"WHOLE_BRIDGE", "全桥"},
    {"OTHER", "其他"},
};

/// 把数据库里的结构部位字面值翻成锚点用的代码；未知值返回空。
std::optional<std::string> structure_part_code(const std::string& label);

enum class PreflightSeverity {
    /// 阻断生成。
    Blocking,
    /// 只提示，不阻断——例如尚未建设的可选桥梁档案字段（设计 §16 末尾）。
    Warning,
};

struct PreflightFinding {
    /// 设计 §23.2 的稳定错误码。
    std::string code;
    std::string message;
    PreflightSeverity severity{PreflightSeverity::Blocking};

    Json::Value to_json() const;
};

/// 生成前检查的结论，同时充当生成页"生成条件"与"内容摘要"两块的数据源（设计 §21.4）。
struct ReportPreflightResult {
    std::string inspection_year_id;
    int inspection_year{0};
    std::vector<PreflightFinding> findings;

    // ---- 内容摘要 --------------------------------------------------------
    /// 有正式病害的构件数。
    int defect_component_count{0};
    /// 正式病害观测条数（拆分后的实例数，不是来源病害数）。
    int defect_observation_count{0};
    /// 去重后的来源病害数。与观测数不同的那部分正是构件范围拆分造成的（设计 §12.2）。
    int source_defect_count{0};
    int photo_count{0};
    std::optional<std::string> overall_grade;
    /// 本次数据实际涉及的结构部位代码，用于与模板锚点比对（§16 第 10 条）。
    std::vector<std::string> structure_parts_with_data;
    /// 所选模板按部位拆分的锚点覆盖了哪些部位。
    std::vector<std::string> template_covered_parts;

    bool can_generate() const;
    Json::Value to_json() const;
};

}  // namespace bridge_report::report
