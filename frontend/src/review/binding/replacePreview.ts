// 批量替换的预览计算。设计见
// docs/superpowers/specs/2026-07-24-bulk-binding-replace-design.md §4。
//
// 全部在前端算：绑定页已把台账 entries 加载在手（手工搜索就用它），无需新增查询接口。

import type { ComponentInventoryEntry } from "../../api/componentInventoryApi";
import type { BindingRow } from "../../api/importBindingApi";
import { normalizeComponentNumber } from "./normalizeComponentNumber";
import { compilePattern } from "./replacePattern";

export type ReplaceOutcome =
  | "will_bind"        // 转换后恰好命中 1 个台账构件
  | "pattern_miss"     // 行文本不符合查找模式
  | "not_in_inventory" // 转换成功，但台账里没有这个编号
  | "ambiguous";       // 转换成功，但台账里有多个同号构件

export interface ReplacePreviewItem {
  componentNumber: string;
  outcome: ReplaceOutcome;
  /** 仅 will_bind / not_in_inventory / ambiguous 有值（pattern_miss 时无从转换）。 */
  targetNumber: string | null;
  /** 仅 will_bind 有值。 */
  bridgeComponentId: string | null;
}

export interface ReplacePreview {
  items: ReplacePreviewItem[];
  bindableCount: number;
  skippedCount: number;
}

export type ReplacePreviewResult =
  | ({ ok: true } & ReplacePreview)
  | { ok: false; error: string };

/** 只有未匹配与歧义参与；已绑定、已标记缺失不参与也不受影响（spec §4.1）。 */
function participates(row: BindingRow): boolean {
  return row.status === "unmatched" || row.status === "ambiguous";
}

export function buildReplacePreview(
  rows: BindingRow[],
  entries: ComponentInventoryEntry[],
  find: string,
  replace: string
): ReplacePreviewResult {
  const compiled = compilePattern(find, replace);
  if (!compiled.ok) return { ok: false, error: compiled.error };

  // 按归一化编号建索引；同号可能有多个，故存数组以便判歧义。
  // 不限定部件类别（spec §4.2）：限定需要报告部件名→规范类别的对照表，那在后端；
  // 不限定的代价只是跨部件同号时判歧义并跳过——宁可跳过让人工处理，不可绑错。
  const byNumber = new Map<string, ComponentInventoryEntry[]>();
  for (const entry of entries) {
    if (!entry.is_active) continue;
    const key = normalizeComponentNumber(entry.component_number);
    const bucket = byNumber.get(key);
    if (bucket) bucket.push(entry);
    else byNumber.set(key, [entry]);
  }

  const items: ReplacePreviewItem[] = [];
  for (const row of rows) {
    if (!participates(row)) continue;

    const targetNumber = compiled.pattern.apply(row.component_number);
    if (targetNumber === null) {
      items.push({
        componentNumber: row.component_number,
        outcome: "pattern_miss",
        targetNumber: null,
        bridgeComponentId: null,
      });
      continue;
    }

    const hits = byNumber.get(normalizeComponentNumber(targetNumber)) ?? [];
    if (hits.length === 1) {
      items.push({
        componentNumber: row.component_number,
        outcome: "will_bind",
        targetNumber,
        bridgeComponentId: hits[0].bridge_component_id,
      });
    } else {
      items.push({
        componentNumber: row.component_number,
        outcome: hits.length === 0 ? "not_in_inventory" : "ambiguous",
        targetNumber,
        bridgeComponentId: null,
      });
    }
  }

  const bindableCount = items.filter((item) => item.outcome === "will_bind").length;
  return { ok: true, items, bindableCount, skippedCount: items.length - bindableCount };
}
