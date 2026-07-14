// 构件类别 -> 主题色。校对工作台的病害卡片与模块 06 构件档案共用同一套配色，
// 保证同一构件类别在两个页面上的视觉身份一致。
const CATEGORY_COLORS: Record<string, string> = {
  "上部承重构件": "#2563a6",
  "上部一般构件": "#16827a",
  "桥墩": "#397a4a",
  "桥台": "#7656a8",
  "翼墙、耳墙": "#a05a7b",
  "桥面铺装": "#b86724",
  "栏杆、护栏": "#46758f",
  "照明、标志": "#8b6b16",
  "河床": "#64748b",
};
const FALLBACK_CATEGORY_COLORS = ["#3f6f9f", "#3f7d68", "#7b5d9b", "#9a6338", "#536f82"];

export function categoryColor(category: string): string {
  if (CATEGORY_COLORS[category]) return CATEGORY_COLORS[category];
  const hash = Array.from(category).reduce((sum, character) => sum + character.codePointAt(0)!, 0);
  return FALLBACK_CATEGORY_COLORS[hash % FALLBACK_CATEGORY_COLORS.length];
}
