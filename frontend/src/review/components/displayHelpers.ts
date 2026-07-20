import type { AttentionItem } from "../grouping";

// 纯展示辅助函数：不修改任何数据，只把领域数据整理成组件可以直接渲染的形状。
// 供 NeedsAttentionSection 使用。

/**
 * 把一条 AttentionItem 格式化为“需要处理”列表里的单行文案：[kind] candidateId: message。
 */
export function formatAttentionItem(item: AttentionItem): string {
  return `[${item.kind}] ${item.candidateId}: ${item.message}`;
}
