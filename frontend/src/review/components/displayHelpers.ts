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
