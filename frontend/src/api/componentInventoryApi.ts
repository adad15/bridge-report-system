import { ApiError, request } from "./apiClient";

export type StructurePart = "superstructure" | "substructure" | "deck_system" | "overall" | "other";

export interface ComponentMapping {
  id: string;
  standard_package_id: string;
  standard_bridge_type_id: string;
  standard_component_category_id: string;
  structure_part: StructurePart;
  mapping_source: string;
  confirmation_status: string;
  is_active: boolean;
}

export interface ComponentInventoryEntry {
  id: string;
  bridge_component_id: string;
  component_number: string;
  site_name: string;
  site_component_type: string;
  span_or_location: string | null;
  is_active: boolean;
  deactivated_at: string | null;
  deactivation_reason: string | null;
  sort_order: number;
  remarks: string | null;
  is_referenced: boolean;
  mappings: ComponentMapping[];
}

export interface ComponentInventoryRevision {
  id: string;
  bridge_id: string;
  revision_number: number;
  status: "草稿" | "已确认" | "draft" | "confirmed" | string;
  baseline_revision_id: string | null;
  confirmed_at: string | null;
  entries: ComponentInventoryEntry[];
}

export interface CatalogPartCountInput {
  key: string;
  label: string;
  hint: string;
}

export interface CatalogPart {
  part_key: string;
  default_name: string;
  structure_part: StructurePart;
  standard_component_category_id: string;
  standard_component_category_name: string;
  number_template: string;
  provisional: boolean;
  // 展开出的位置真实桥上不一定都有（翼墙/锥坡/护坡），向导逐个给复选框。
  instance_selectable: boolean;
  count_inputs: CatalogPartCountInput[];
}

export interface PartSelection {
  part_key: string;
  site_name: string;
  counts: number[];
  excluded_numbers?: string[];
}

export interface GenerateComponentInventoryInput {
  standard_package_id: string;
  bridge_type_id: string;
  span_count: number;
  part_selections: PartSelection[];
}

export interface InventoryEntryInput {
  component_number: string;
  site_name: string;
  site_component_type: string;
  span_or_location?: string | null;
  remarks?: string | null;
  sort_order?: number;
}

export interface InventoryMappingInput {
  standard_package_id: string;
  standard_bridge_type_id: string;
  standard_component_category_id: string;
  structure_part: StructurePart;
  mapping_source?: string;
}

export interface InventoryBlocker {
  code: string;
  entity_type: string;
  entity_id: string;
  field_path: string;
  message: string;
}


// ---- 聚合接口 ----
// 台账页首屏只要这份分组汇总；构件明细按需另取。整份修订版仍由 /latest 提供，
// 校对工作台的构件选择器在用，本轮不动。

export interface InventoryGroupSummary {
  site_component_type: string;
  structure_part: StructurePart;
  active_count: number;
  // 整组构件被全部停用时为 null（编号范围只统计启用构件）。
  first_number: string | null;
  last_number: string | null;
  confirmed_count: number;
  pending_count: number;
  unmapped_count: number;
  // 该组没有任何生效映射时为 null。
  standard_package_id: string | null;
  standard_component_category_id: string | null;
  // 与类别同出一条生效映射，两者同为 null 或同非 null。
  // 查评定树适用病害要的是 (桥型, 类别) 这一对，缺了桥型只能靠下载整份台账去翻。
  standard_bridge_type_id: string | null;
}

export interface InventoryBlockerSample {
  code: string;
  entity_type: string;
  entity_id: string;
  field_path: string;
  message: string;
  // inventory_empty 这类修订版级问题没有这两项。
  site_component_type: string | null;
  position: number | null;
}

export interface InventoryBlockerSummary {
  total: number;
  // 逐条列出的那部分（无映射构件 + 空台账）。"其余 N 项"要用它减样本数，
  // 用 total 会把待确认那批数两遍。
  individual_total: number;
  by_code: { inventory_empty: number; component_mapping_required: number };
  samples: InventoryBlockerSample[];
}

export interface InventoryRevisionSummary {
  id: string;
  bridge_id: string;
  revision_number: number;
  status: string;
  baseline_revision_id: string | null;
  confirmed_at: string | null;
  active_entry_count: number;
}

export interface InventorySummary {
  revision: InventoryRevisionSummary;
  groups: InventoryGroupSummary[];
  blockers: InventoryBlockerSummary;
}

// 分组分页与编号搜索返回的构件，多带一个组内序号。
export interface LocatedInventoryEntry extends ComponentInventoryEntry {
  position: number;
}

export interface InventoryGroupEntriesResponse {
  total: number;
  page: number;
  size: number;
  entries: LocatedInventoryEntry[];
}

export interface InventorySearchResponse {
  // 未截断的命中数，界面上"匹配 N 个构件，显示前 M 个"依赖它。
  total: number;
  entries: LocatedInventoryEntry[];
}

// 写操作的响应：新的汇总，外加被改动的那一条构件（删除、批量确认、确认台账没有）。
export interface InventoryWriteResult extends InventorySummary {
  entry?: LocatedInventoryEntry;
  entry_id?: string;
}

const json = (method: string, body: unknown): RequestInit => ({
  method,
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify(body),
});

async function revisionRequest(url: string, init?: RequestInit): Promise<ComponentInventoryRevision> {
  return (await request<{ revision: ComponentInventoryRevision }>(url, init)).revision;
}

// 除生成台账外的写操作都回传这个形状。
function writeRequest(url: string, init?: RequestInit): Promise<InventoryWriteResult> {
  return request<InventoryWriteResult>(url, init);
}

export function fetchInventorySummary(baseUrl: string, bridgeId: string) {
  return request<InventorySummary>(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/component-inventories/latest/summary`);
}

// 写操作可能派生出新的修订版，之后所有请求都要用响应里带回的那个 id。
export function fetchInventorySummaryByRevision(baseUrl: string, revisionId: string) {
  return request<InventorySummary>(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/summary`);
}

export function fetchInventoryGroupEntries(
  baseUrl: string,
  revisionId: string,
  siteComponentType: string,
  page: number,
  size: number,
  signal?: AbortSignal
) {
  const query = new URLSearchParams({
    group: siteComponentType, page: String(page), size: String(size),
  });
  return request<InventoryGroupEntriesResponse>(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/entries?${query}`,
    { signal });
}

/**
 * 关键词检索，匹配编号、构件类别、现场名称三个字段。
 *
 * bindingEligible：只返回启用且有生效映射的构件，绑定面板传 true。过滤在服务端、在
 * limit 之前生效——先取前 N 条再由前端筛的话，这 N 条可能全是停用构件，真正可绑的
 * 被截断在后面。它表示"台账层面可供选择"，**不保证**对某一行可绑：部件名与规范类别
 * 的兼容性仍由后端在正式绑定时判定。
 */
export function searchInventoryEntries(
  baseUrl: string,
  revisionId: string,
  keyword: string,
  limit: number,
  signal?: AbortSignal,
  bindingEligible = false
) {
  const query = new URLSearchParams({ keyword, limit: String(limit) });
  if (bindingEligible) query.set("binding_eligible", "true");
  return request<InventorySearchResponse>(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/entries?${query}`,
    { signal });
}

export function fetchLatestComponentInventory(baseUrl: string, bridgeId: string) {
  return revisionRequest(`${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/component-inventories/latest`);
}

export async function fetchPartCatalog(
  baseUrl: string,
  standardPackageId: string,
  bridgeTypeId: string
): Promise<CatalogPart[]> {
  const query = new URLSearchParams({
    standard_package_id: standardPackageId,
    bridge_type_id: bridgeTypeId,
  });
  return (
    await request<{ parts: CatalogPart[] }>(`${baseUrl}/api/component-inventories/part-catalog?${query}`)
  ).parts;
}

export function generateComponentInventory(baseUrl: string, bridgeId: string, input: GenerateComponentInventoryInput) {
  return writeRequest(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/component-inventories/generate`,
    json("POST", input)
  );
}

export function addComponentInventoryEntry(baseUrl: string, revisionId: string, input: InventoryEntryInput) {
  return writeRequest(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/entries`,
    json("POST", input)
  );
}

export function updateComponentInventoryEntry(
  baseUrl: string,
  revisionId: string,
  entryId: string,
  input: InventoryEntryInput
) {
  return writeRequest(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/entries/${encodeURIComponent(entryId)}`,
    json("PATCH", input)
  );
}

export function deleteComponentInventoryEntry(baseUrl: string, revisionId: string, entryId: string) {
  return writeRequest(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/entries/${encodeURIComponent(entryId)}`,
    { method: "DELETE" }
  );
}

export function deactivateComponentInventoryEntry(
  baseUrl: string,
  revisionId: string,
  entryId: string,
  reason: string
) {
  return writeRequest(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/entries/${encodeURIComponent(entryId)}/deactivate`,
    json("POST", { reason })
  );
}

export function setComponentInventoryMapping(
  baseUrl: string,
  revisionId: string,
  entryId: string,
  input: InventoryMappingInput
) {
  return writeRequest(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/entries/${encodeURIComponent(entryId)}/mapping`,
    json("PUT", input)
  );
}

export function confirmPendingComponentInventoryMappings(
  baseUrl: string,
  revisionId: string,
  siteComponentType?: string
) {
  return writeRequest(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/mappings/confirm-pending`,
    json("POST", siteComponentType ? { site_component_type: siteComponentType } : {})
  );
}

export function confirmComponentInventory(baseUrl: string, revisionId: string, note = "") {
  return writeRequest(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/confirm`,
    json("POST", { note })
  );
}

export function componentInventoryErrorMessage(error: unknown): string {
  if (!(error instanceof ApiError)) return "构件台账操作失败，请稍后重试。";
  const messages: Record<string, string> = {
    component_inventory_not_found: "这座桥还没有构件台账，请先生成初始台账。",
    component_inventory_conflict: "台账已变化或存在重复编号，请刷新后检查。",
    inventory_revision_superseded: "台账已有基于其他版本的草稿，已为你切到最新草稿，请重新操作。",
    component_is_referenced: "该构件已被病害或正式项目引用，只能停用。",
    component_inventory_confirmation_blocked: "台账仍有未解决项，暂时不能确认。",
    standard_package_unavailable: "所选技术评定规范包当前不可用。",
  };
  return messages[error.code] ?? error.message;
}
