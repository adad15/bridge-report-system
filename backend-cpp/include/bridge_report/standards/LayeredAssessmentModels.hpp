#pragma once

#include <optional>
#include <string>
#include <vector>

// 问题和计算轨迹两个通用结构直接复用 H21 的定义，H21 的模型本身不动。
#include "bridge_report/standards/AssessmentModels.hpp"

namespace bridge_report::standards {

// 分层评定（构件 → 桥跨 → 评定单元 → 全桥）的输入输出，JTG/T 5230—2026 使用。

struct LayeredUnitInput {
    std::string unit_id;
    std::string evaluation_type_id;
    std::string weight_set_id;
    std::vector<std::string> span_ids;               // 按桥跨顺序；桥跨 id 在全桥唯一
    std::vector<std::string> not_required_part_ids;  // B.1.3 全桥无须设置的部件
};

struct LayeredComponentInput {
    std::string component_id;
    std::string part_id;
    std::vector<std::string> span_ids;     // 共用构件列多个；所在单元由桥跨推出
    std::optional<int> eddi;               // 耐久性缺损 0～2
    std::optional<int> esdi;               // 结构性缺损 0～3
    std::optional<int> efi;                // 功能状况 0～2
    std::optional<int> eai;                // 影响状况 −1～1，只对主要构件生效
    bool missing{false};                   // 应设未设（3.2.2-5），EDI 取 3
    bool affects_bridge_safety{false};     // 3.3.3 改判条件，人工判断
    std::optional<bool> is_main;           // 只用于部分为主要构件的部件，且此时必填
};

struct LayeredAssessmentInput {
    std::vector<LayeredUnitInput> units;
    std::vector<LayeredComponentInput> components;
    std::vector<std::string> triggered_control_ids;
};

struct LayeredComponentResult {
    std::string component_id;
    std::string part_id;
    bool is_main{false};
    int edi{0};
    int eci{0};
    int ecl{0};
    std::string state;                                // 优 / 良 / 中 / 次 / 差
    std::vector<std::string> maintenance_strategies;
    bool eai_ignored{false};                          // 非主要构件给了 EAI
};

struct LayeredPartResult {
    std::string part_id;
    std::string table_name;          // 表内原名
    std::string structure;           // deck_system / superstructure / substructure / ancillary
    int ci{0};
    std::string worst_component_id;  // 该跨没有这个部件的构件时为空
    double effective_omega{0.0};     // 以下三项只供展示，计算用精确分数
    double gamma{0.0};
    double contribution{0.0};        // CI·ω·γ/5
};

struct LayeredSpanResult {
    std::string span_id;
    std::vector<LayeredPartResult> parts;   // 按权重套顺序，不含无须设置的部件
    double calculated_index{0.0};           // 式 3.3.1，一位小数
    int calculated_grade{0};                // 表 3.3.2
    double index{0.0};                      // 改判后的值；未改判时同上
    int grade{0};
    std::string override_component_id;      // 3.3.3 改判依据的构件；未改判为空
};

struct LayeredUnitResult {
    std::string unit_id;
    std::string evaluation_type_id;
    std::string weight_set_id;
    std::vector<LayeredSpanResult> spans;
    double index{0.0};
    int grade{0};
    std::string rule_clause;                // "3.4.1" / "3.4.2" / "3.4.3"
};

struct LayeredControlResult {
    std::string control_id;
    std::string source_clause;
    std::string label;
};

struct LayeredAssessmentResult {
    std::string standard_id;
    std::string package_version;
    std::vector<LayeredComponentResult> components;
    std::vector<LayeredUnitResult> units;
    std::string governing_unit_id;          // 3.4.4 最差单元
    double layered_index{0.0};              // 分层综合评定的全桥指数
    int layered_grade{0};
    std::vector<LayeredControlResult> triggered_controls;
    int final_grade{0};                     // 有单项控制时为 5
    std::vector<AssessmentTraceEntry> trace;
    std::string explanation;
};

struct LayeredAssessmentOutcome {
    std::optional<LayeredAssessmentResult> result;
    std::vector<AssessmentIssue> issues;

    bool ok() const noexcept { return result.has_value() && issues.empty(); }
};

}  // namespace bridge_report::standards
