#pragma once

#include <string>
#include <vector>

namespace bridge_report::rating_tree {

// 匹配比较键：只用于比较，绝不回写成用户看到的病害类型或描述。
//
// 允许的规范化仅限：去除首尾空白、合并连续空白、统一全角/半角标点、统一括号与逗号、
// 去掉只位于首尾的无语义分隔符。文字内部有业务含义的数字、单位、`/` 和 `-`
// （L/W=2、1-1#板、板底/腹板交界处）必须原样保留。
[[nodiscard]] std::string normalize_match_key(const std::string& value);

// 按标点把一段病害叙述切成语义片段，供"同一条记录命中多个病害节点"的组合判定使用。
// 每个片段都已经过 normalize_match_key，空片段会被丢弃。
[[nodiscard]] std::vector<std::string> split_match_segments(const std::string& value);

}  // namespace bridge_report::rating_tree
