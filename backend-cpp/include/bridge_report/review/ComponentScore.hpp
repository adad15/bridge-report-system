#pragma once

#include <optional>
#include <string>
#include <vector>

namespace bridge_report::review {

// JTG/T H21-2011 第 4.1.1 条构件技术状况评分。
// 纯函数，不访问数据库；与 Python bridge_report_tools.scoring.component_score、
// 前端 src/review/componentScore.ts 逐行同构，共享夹具
// samples/scoring/component_score_cases.json 保证跨语言一致。
struct ComponentScoreResult {
    // 未舍入的构件评分，计算全程不得提前舍入。
    double score{0.0};
    // 参与计算的病害扣分，降序排列。
    std::vector<double> ordered_deductions;
};

// 输入为空或任一扣分不在 (0, 100] 内时无法计算，返回空。
// 输入顺序不影响结果：内部先降序排序再累计；任一 DP=100 时评分为 0。
std::optional<ComponentScoreResult> compute_component_score(const std::vector<double>& deductions);

// 两位小数、半数远离零的舍入；禁用语言内建 round 的默认平/半舍规则。
double round_score_to_two_decimals(double value);

// 按两位小数比较来源分与复算分：复算缺失 -> 无法复算；
// 来源缺失 -> 不一致（必须人工显式处理）；round2 相等 -> 一致，否则 -> 不一致。
std::string classify_score_validation(
    std::optional<double> source_score,
    std::optional<double> calculated_score
);

}  // namespace bridge_report::review
