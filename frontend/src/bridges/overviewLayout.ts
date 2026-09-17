/**
 * 桥梁概览两张长卡片（年度病害对比、桥梁概况）正文区的限高。
 *
 * 四张卡片要在一屏里见到：屏幕高就多给几行，屏幕矮就收，不把整页顶出竖滚动条。
 * 减掉的是外壳占掉的高度（顶栏 + 内容区上下留白，来自 design-system 的 `pageOffset`）
 * 再加这一页自己的指标行与两行卡片的标题、间距。
 */
const OVERVIEW_CHROME = 504;

export function overviewScrollHeight(pageOffset: number): string {
  return `clamp(118px, calc(100vh - ${pageOffset + OVERVIEW_CHROME}px), 420px)`;
}
