// 模板配置里「表号 / 图号格式」的默认键集（设计 §7.5、§21.1）。
//
// **这份清单是便利，不是权威。** 真正的权威是 Python 侧的模板契约：多配一个键会被
// 判 `template_number_format_unknown_block`，少配一个会被判
// `template_number_format_missing`，两种都会让上传当场失败并指名道姓。所以这里漂了
// 也不会静默出错，只会让管理员在上传时多改一行。

export interface NumberFormatRow {
  key: string;
  label: string;
  format: string;
}

/**
 * `periodic_inspection_v1` 的编号键。
 *
 * 按部位重复的块（病害表、病害照片）每个部位各一条，于是各自从 1 起排，与正式报告
 * 一致；整篇只出现一次的块各占一条。4.1.1 的部件权重表和 4.1.2 的评定表**共用同一个
 * 格式串**，因此自然得到 表4.1-1 与 表4.1-2——共号与否由格式串决定，不写死在代码里。
 */
export const PERIODIC_INSPECTION_V1_NUMBER_FORMATS: NumberFormatRow[] = [
  { key: "DEFECT_TABLES:SUPERSTRUCTURE", label: "上部结构 · 病害表", format: "表2.1-{n}" },
  { key: "DEFECT_TABLES:SUBSTRUCTURE", label: "下部结构 · 病害表", format: "表2.2-{n}" },
  { key: "DEFECT_TABLES:DECK", label: "桥面系 · 病害表", format: "表2.3-{n}" },
  { key: "DEFECT_PHOTOS:SUPERSTRUCTURE", label: "上部结构 · 病害照片", format: "照片2.1-{n}" },
  { key: "DEFECT_PHOTOS:SUBSTRUCTURE", label: "下部结构 · 病害照片", format: "照片2.2-{n}" },
  { key: "DEFECT_PHOTOS:DECK", label: "桥面系 · 病害照片", format: "照片2.3-{n}" },
  { key: "COMPONENT_WEIGHTS", label: "4.1.1 部件权重分配表", format: "表4.1-{n}" },
  { key: "ASSESSMENT_RESULT", label: "4.1.2 技术状况等级表", format: "表4.1-{n}" },
  { key: "ASSESSMENT_APPENDIX", label: "附录1 技术状况评定卡片", format: "附表1-{n}" },
];

/** 模板必须配置的人员角色。签字页按它决定哪些角色是必填项（设计 §15.3）。 */
export const PERSONNEL_ROLE_OPTIONS = [
  { value: "approver", label: "批准" },
  { value: "reviewer", label: "审核" },
  { value: "lead_inspector", label: "检测负责人" },
  { value: "compiler", label: "编制" },
  { value: "participant", label: "参加人员" },
];

export const DEFAULT_REQUIRED_ROLES = ["approver", "reviewer", "lead_inspector", "compiler"];

export function rowsToFormatMap(rows: NumberFormatRow[]): Record<string, string> {
  const map: Record<string, string> = {};
  for (const row of rows) {
    const key = row.key.trim();
    const format = row.format.trim();
    if (key && format) map[key] = format;
  }
  return map;
}

export function formatMapToRows(map: Record<string, string> | undefined): NumberFormatRow[] {
  const known = new Map(PERIODIC_INSPECTION_V1_NUMBER_FORMATS.map((row) => [row.key, row.label]));
  return Object.entries(map ?? {}).map(([key, format]) => ({
    key,
    label: known.get(key) ?? key,
    format,
  }));
}
