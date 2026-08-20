import { request } from "./apiClient";

export type ComponentBindingStatus = "bound" | "unmatched" | "ambiguous" | "missing";

/** 下拉里显示一个构件所需的全部信息。entry_id 是 <option> 的 key，bridge_component_id 是 value。 */
export interface BindingComponentSummary {
  entry_id: string;
  bridge_component_id: string;
  component_number: string;
  site_component_type: string;
  site_name: string;
}

/** 下拉里的"两侧"选项：选中即把该行拆开，一次绑到左右两件。 */
export interface SidePairOption {
  label: string;
  bridge_component_ids: string[];
}

export interface BindingRow {
  component_number: string;
  defect_count: number;
  status: ComponentBindingStatus;
  bridge_component_id: string | null;
  /**
   * 概览直接带回可显示的构件信息，前端不必再为把 id 换成编号去拉整份台账。
   * 只含"启用且有生效映射"的构件，所以可能比后端的内部候选数少——展示对象缺失
   * 不改变行状态，歧义行仍然是歧义行。
   */
  bound_component: BindingComponentSummary | null;
  candidate_components: BindingComponentSummary[];
  split_eligible?: boolean;
  split_expanded_count?: number | null;
  /**
   * 该行可作为"两侧"整体绑定时后端给出的选项；不成立时为 null。
   * label 由后端拼好（它要点名将绑给哪两件），前端原样显示，不自己拼。
   */
  side_pair_option?: SidePairOption | null;
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

/**
 * 写操作的请求体 + 编辑锁令牌。后端六个绑定写接口都要求持锁：它们改的是
 * import_records.parsed_result_json，与校对草稿保存写的是同一份数据，
 * 不持锁写进去的修改会被持锁者的整份保存覆盖掉。
 *
 * lockToken 是必填参数而不是可选字段——可选的话，漏传的调用点会安静地拿到 409，
 * 而不是在编译期被拦下。
 */
const lockedJson = (method: string, body: unknown, lockToken: string): RequestInit => ({
  method,
  headers: { "Content-Type": "application/json", "X-Edit-Lock-Token": lockToken },
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
  expectedInventoryRevisionId: string,
  lockToken: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/rating-tree"),
    lockedJson("POST", {
      rating_tree_version_id: ratingTreeVersionId,
      expected_inventory_revision_id: expectedInventoryRevisionId,
    }, lockToken)
  );
}

export function bindComponent(
  baseUrl: string,
  importId: string,
  input: BindingTarget & { bridge_component_id: string },
  expectedInventoryRevisionId: string,
  lockToken: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/bind"),
    lockedJson("POST", { ...input, expected_inventory_revision_id: expectedInventoryRevisionId }, lockToken)
  );
}

/**
 * "两侧"绑定：把一行病害拆到多个实际构件上，每条各自绑定。
 *
 * 与 bindComponent 的关键区别：它会**增删病害与照片候选**（一行 N 条拆成 N×M 条），
 * 所以调用方写完必须重取校对草稿，不能沿用手里那份。工作区里走同一个 run()，
 * onDraftInvalidated 会自动触发。
 */
export function bindComponentsMulti(
  baseUrl: string,
  importId: string,
  input: BindingTarget & { bridge_component_ids: string[] },
  expectedInventoryRevisionId: string,
  lockToken: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/bind-multi"),
    lockedJson("POST", { ...input, expected_inventory_revision_id: expectedInventoryRevisionId }, lockToken)
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
  expectedInventoryRevisionId: string,
  lockToken: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/bind-batch"),
    lockedJson("POST", { targets, expected_inventory_revision_id: expectedInventoryRevisionId }, lockToken)
  );
}

export function markComponentMissing(
  baseUrl: string,
  importId: string,
  input: BindingTarget,
  expectedInventoryRevisionId: string,
  lockToken: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/mark-missing"),
    lockedJson("POST", { ...input, expected_inventory_revision_id: expectedInventoryRevisionId }, lockToken)
  );
}

export function clearComponentBinding(
  baseUrl: string,
  importId: string,
  input: BindingTarget,
  expectedInventoryRevisionId: string,
  lockToken: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/clear"),
    lockedJson("POST", { ...input, expected_inventory_revision_id: expectedInventoryRevisionId }, lockToken)
  );
}

/** 批量替换预览用的精简条目，见 buildReplacePreview。 */
export interface BindingReplaceInventoryEntry {
  bridge_component_id: string;
  component_number: string;
  is_active: boolean;
}

export interface BindingReplaceInventory {
  inventory_revision_id: string;
  entries: BindingReplaceInventoryEntry[];
}

/**
 * 批量替换取数。只在打开对话框时调；服务端只校验版本、不锁定年度。
 * 响应只含预览真正用得上的三个字段，整份台账比它大一个数量级。
 */
export function fetchBindingReplaceInventory(
  baseUrl: string,
  importId: string,
  expectedInventoryRevisionId: string,
  signal?: AbortSignal
) {
  const query = new URLSearchParams({
    expected_inventory_revision_id: expectedInventoryRevisionId,
  });
  return request<BindingReplaceInventory>(
    bindingUrl(baseUrl, importId, `/inventory?${query}`),
    { signal }
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
  expectedInventoryRevisionId: string,
  lockToken: string
) {
  return request<ComponentRangeSplitApply>(
    bindingUrl(baseUrl, importId, "/split-apply"),
    lockedJson("POST", {
      targets,
      impact_token: impactToken,
      expected_inventory_revision_id: expectedInventoryRevisionId,
    }, lockToken)
  );
}
