#pragma once

#include <string>
#include <vector>

namespace bridge_report::inventory {

// 报告"部件名称"列（规范固定用词）→ 规范评定类别 id。
// 跨桥型真正同名者（横向联结系 / 索塔 / 桥面板）返回多候选，靠该桥台账实际类别集合消歧。
// 入参做无害归一化（去首尾/内部空白、去括号内示例/别名），未知名返回空。
[[nodiscard]] std::vector<std::string> resolve_component_categories(const std::string& part_name);

}  // namespace bridge_report::inventory
