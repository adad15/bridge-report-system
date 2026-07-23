// 结构分部的显示名与排列顺序。向导（选构件）与台账（核对构件）必须一致，
// 否则同一座桥在两个页面里的部件顺序会对不上。
export const structurePartLabels: Record<string, string> = {
  superstructure: "上部结构",
  substructure: "下部结构",
  deck_system: "桥面系",
  overall: "整体",
  other: "其他",
};

export const structurePartOrder = [
  "superstructure",
  "substructure",
  "deck_system",
  "overall",
  "other",
];

export function structurePartLabel(key: string): string {
  return structurePartLabels[key] ?? key;
}
