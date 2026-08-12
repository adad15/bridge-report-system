// 纯展示辅助函数：不修改任何数据，只把领域数据整理成组件可以直接渲染的形状。

/**
 * 病害位置在检测 Word 里惯用 "/" 表示"无此项"，原样搬到界面上，每行都挂一个斜杠，
 * 纯是噪声。返回 null 表示这条位置不值得占一行；只做展示判断，不动原始数据。
 */
export function displayDefectLocation(location: string | null | undefined): string | null {
  const trimmed = location?.trim() ?? "";
  if (trimmed === "" || trimmed === "/" || trimmed === "／") return null;
  return trimmed;
}

/**
 * 匹配依据要不要显示，以及显示成什么样。
 *
 * 精确匹配和受控别名不给依据：节点名和方式标签已经把话说完了，再写一句
 * "命中受控别名XX"只是把同一个词重复一遍。关键词与叙述片段两层要保留——
 * 它们指出的是描述里的哪一段触发了匹配，那句话在别处看不到。
 *
 * 存量草稿里存着两段已经废弃的样板：范围说明恒为真（范围过滤是第一道工序，
 * 通过是结果存在的前提），规则编号是排查用的内部标识。两者都在这里滤掉，
 * 免得已经校对过、不会再重新匹配的病害永远顶着旧文字。
 */
export function displayMatchEvidence(
  evidence: string | null | undefined,
  matchMethod: string | null | undefined,
): string {
  if (matchMethod === "exact" || matchMethod === "controlled_alias") return "";
  return (evidence ?? "")
    .replace(/\s*构件适用依据：[^。]*。/g, "")
    .replace(/（规则\s*[^）]*）/g, "")
    .trim();
}
