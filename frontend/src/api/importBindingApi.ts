import { request } from "./apiClient";

export type ComponentBindingStatus = "bound" | "unmatched" | "ambiguous" | "missing";

export interface BindingRow {
  component_number: string;
  defect_count: number;
  status: ComponentBindingStatus;
  bridge_component_id: string | null;
  candidate_component_ids: string[];
  split_eligible?: boolean;
  split_expanded_count?: number | null;
}

export interface BindingGroup {
  part_name: string;
  total: number;
  bound: number;
  unmatched: number;
  ambiguous: number;
  missing: number;
  rows: BindingRow[];
}

export interface ComponentBindingOverview {
  inventory_confirmed: boolean;
  /**
   * 本次概览所依据的台账版本。契约不变量：非 null 当且仅当 inventory_confirmed 为真。
   * 后续搜索寻址、批量缓存键与所有写操作的 expected_inventory_revision_id 都取这里。
   */
  inventory_revision_id: string | null;
  rating_tree?: {
    version_id: string;
    tree_name: string;
    package_version: string;
    h21_package_version: string;
    maintenance_package_version: string;
  } | null;
  groups: BindingGroup[];
}

export interface BindingTarget {
  part_name: string;
  component_number: string;
}

export interface ComponentRangeSplitItem extends BindingTarget {
  expanded_component_count: number;
  source_defect_count: number;
  result_defect_count: number;
  result_photo_count: number;
  bound_count: number;
  ambiguous_count: number;
  unmatched_count: number;
}

export interface ComponentRangeSplitTotals {
  selected_range_count: number;
  source_defect_count: number;
  result_defect_count: number;
  result_photo_count: number;
  bound_count: number;
  ambiguous_count: number;
  unmatched_count: number;
}

export interface ComponentRangeSplitPreview {
  items: ComponentRangeSplitItem[];
  totals: ComponentRangeSplitTotals;
  impact_token: string;
}

export interface ComponentRangeSplitApply extends ComponentRangeSplitPreview {
  operation_id: string;
  overview: ComponentBindingOverview;
}

// 已处理 = 已绑定或已标记缺失。绑定分区页头的"已处理 x / 共 y"与校对页侧栏的
// 待处理计数共用这一口径，避免两处各算各的。
export function bindingProgress(overview: ComponentBindingOverview): {
  total: number;
  resolved: number;
  pending: number;
} {
  let total = 0;
  let resolved = 0;
  for (const group of overview.groups) {
    for (const row of group.rows) {
      total += 1;
      if (row.status === "bound" || row.status === "missing") resolved += 1;
    }
  }
  return { total, resolved, pending: total - resolved };
}

const json = (method: string, body: unknown): RequestInit => ({
  method,
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify(body),
});

async function overviewRequest(url: string, init?: RequestInit): Promise<ComponentBindingOverview> {
  return (await request<{ overview: ComponentBindingOverview }>(url, init)).overview;
}

function bindingUrl(baseUrl: string, importId: string, suffix = ""): string {
  return `${baseUrl}/api/import-records/${encodeURIComponent(importId)}/component-binding${suffix}`;
}

export function fetchComponentBinding(baseUrl: string, importId: string) {
  return overviewRequest(bindingUrl(baseUrl, importId));
}

/**
 * 后端在事务里比对这个版本；不一致就返回该错误码，不静默改用新版本。
 * 三条路径（绑定、范围拆分、评定树）用的是同一个码，前端一处接住即可。
 */
export const INVENTORY_REVISION_CHANGED = "component_inventory_revision_changed";

/**
 * 每个写操作都必须声明"本次依据的是哪个台账版本"，所以这是必填参数而不是可选字段——
 * 可选的话，漏传的调用点会安静地退回到旧的"服务端自己挑一个版本"行为。
 * 取值一律是 overview.inventory_revision_id。
 */
export function bindInspectionRatingTree(
  baseUrl: string,
  importId: string,
  ratingTreeVersionId: string,
  expectedInventoryRevisionId: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/rating-tree"),
    json("POST", {
      rating_tree_version_id: ratingTreeVersionId,
      expected_inventory_revision_id: expectedInventoryRevisionId,
    })
  );
}

export function bindComponent(
  baseUrl: string,
  importId: string,
  input: BindingTarget & { bridge_component_id: string },
  expectedInventoryRevisionId: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/bind"),
    json("POST", { ...input, expected_inventory_revision_id: expectedInventoryRevisionId })
  );
}

/**
 * 批量绑定（批量替换用）。后端整批原子：任一目标非法则一条都不写，
 * 并在 details.rejected_component_number 指明是哪一条挡住的。
 * 版本字段放在请求根节点，不逐个 target 重复。
 */
export function bindComponentsBatch(
  baseUrl: string,
  importId: string,
  targets: (BindingTarget & { bridge_component_id: string })[],
  expectedInventoryRevisionId: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/bind-batch"),
    json("POST", { targets, expected_inventory_revision_id: expectedInventoryRevisionId })
  );
}

export function markComponentMissing(
  baseUrl: string,
  importId: string,
  input: BindingTarget,
  expectedInventoryRevisionId: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/mark-missing"),
    json("POST", { ...input, expected_inventory_revision_id: expectedInventoryRevisionId })
  );
}

export function clearComponentBinding(
  baseUrl: string,
  importId: string,
  input: BindingTarget,
  expectedInventoryRevisionId: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/clear"),
    json("POST", { ...input, expected_inventory_revision_id: expectedInventoryRevisionId })
  );
}

export function previewComponentRangeSplit(
  baseUrl: string,
  importId: string,
  targets: BindingTarget[],
  expectedInventoryRevisionId: string
) {
  return request<ComponentRangeSplitPreview>(
    bindingUrl(baseUrl, importId, "/split-preview"),
    json("POST", { targets, expected_inventory_revision_id: expectedInventoryRevisionId })
  );
}

export function applyComponentRangeSplit(
  baseUrl: string,
  importId: string,
  targets: BindingTarget[],
  impactToken: string,
  expectedInventoryRevisionId: string
) {
  return request<ComponentRangeSplitApply>(
    bindingUrl(baseUrl, importId, "/split-apply"),
    json("POST", {
      targets,
      impact_token: impactToken,
      expected_inventory_revision_id: expectedInventoryRevisionId,
    })
  );
}
