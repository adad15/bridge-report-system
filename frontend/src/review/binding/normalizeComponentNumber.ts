// 构件编号归一化。**权威实现在 C++**：
// `backend-cpp/src/inventory/ComponentMatcher.cpp` 的 `normalize_component_number`。
// 此处为逐步镜像，供批量替换的预览在前端判断"转换后的编号能否命中台账"。
//
// 两边必须同规则：若分叉，症状是"预览说能绑、后端却判无此编号"，且只在含全角字符或
// 异常空白的行上出现，极难定位。改动任一侧都要同步另一侧。

export function normalizeComponentNumber(value: string): string {
  let normalized = value.trim();
  normalized = normalized.split("－").join("-");
  normalized = normalized.split("–").join("-");
  normalized = normalized.split("—").join("-");
  normalized = normalized.split("＃").join("#");
  normalized = normalized.split("　").join("");
  // C++ 用 isspace 去掉所有空白字符（含制表符、换行）。
  normalized = normalized.replace(/\s/g, "");
  normalized = normalized.toLowerCase();
  // C++ 只剥尾部的 '#'，可能有多个。
  normalized = normalized.replace(/#+$/, "");
  return normalized;
}
