#pragma once

#include <optional>
#include <string>
#include <vector>

// 构件绑定的批量查找替换：把报告编号按规则变换成台账编号，用于查找绑定目标。
// 设计见 docs/superpowers/specs/2026-07-24-bulk-binding-replace-design.md §3.1。
//
// 刻意不引入正则语法：查找串里除 `*` 外一律按字面处理，用户不必了解转义。
// `*` 匹配一段连续数字（至少一位），替换串第 k 个 `*` 取第 k 段数字。
//
// 这是**权威实现**。5.0 之前前端也有一份（`replacePattern.ts`），两份规则一旦分叉，
// 症状是"预览说能绑、后端却判无此编号"，且只在个别行上复现。前端那份随本次拆分删除。
namespace bridge_report::inventory {

class ComponentReplacePattern {
public:
    /// 编译失败时返回 nullopt，并把可直接展示的原因写进 error。
    [[nodiscard]] static std::optional<ComponentReplacePattern> compile(
        const std::string& find, const std::string& replace, std::string& error);

    /// 命中返回替换结果；不命中返回 nullopt（与"替换成空串"区分开）。
    [[nodiscard]] std::optional<std::string> apply(const std::string& text) const;

private:
    ComponentReplacePattern() = default;

    /// 按 `*` 切开的字面段；相邻两段之间是一段连续数字。
    std::vector<std::string> find_segments_;
    std::vector<std::string> replace_segments_;
};

}  // namespace bridge_report::inventory
