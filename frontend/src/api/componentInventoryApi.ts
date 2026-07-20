import { ApiError, request } from "./apiClient";

export type StructurePart = "superstructure" | "substructure" | "deck_system" | "overall" | "other";
export type NumberingMode = "span_member" | "pier_line" | "sequential";

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

export interface InventoryGenerationGroup {
  site_component_type: string;
  site_name: string;
  standard_component_category_id: string;
  structure_part: StructurePart;
  numbering_mode: NumberingMode;
  quantity: number;
  quantity_key: string;
  number_prefix?: string;
  number_suffix?: string;
}

export interface GenerateComponentInventoryInput {
  standard_package_id: string;
  template_id: string;
  bridge_type_id: string;
  span_count: number;
  input_quantities: Record<string, number>;
  groups: InventoryGenerationGroup[];
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

const json = (method: string, body: unknown): RequestInit => ({
  method,
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify(body),
});

async function revisionRequest(url: string, init?: RequestInit): Promise<ComponentInventoryRevision> {
  return (await request<{ revision: ComponentInventoryRevision }>(url, init)).revision;
}

export function fetchLatestComponentInventory(baseUrl: string, bridgeId: string) {
  return revisionRequest(`${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/component-inventories/latest`);
}

export function fetchComponentInventory(baseUrl: string, revisionId: string) {
  return revisionRequest(`${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}`);
}

export function generateComponentInventory(baseUrl: string, bridgeId: string, input: GenerateComponentInventoryInput) {
  return revisionRequest(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/component-inventories/generate`,
    json("POST", input)
  );
}

export function addComponentInventoryEntry(baseUrl: string, revisionId: string, input: InventoryEntryInput) {
  return revisionRequest(
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
  return revisionRequest(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/entries/${encodeURIComponent(entryId)}`,
    json("PATCH", input)
  );
}

export function deleteComponentInventoryEntry(baseUrl: string, revisionId: string, entryId: string) {
  return revisionRequest(
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
  return revisionRequest(
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
  return revisionRequest(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/entries/${encodeURIComponent(entryId)}/mapping`,
    json("PUT", input)
  );
}

export function confirmComponentInventory(baseUrl: string, revisionId: string, note = "") {
  return revisionRequest(
    `${baseUrl}/api/component-inventories/${encodeURIComponent(revisionId)}/confirm`,
    json("POST", { note })
  );
}

export function componentInventoryErrorMessage(error: unknown): string {
  if (!(error instanceof ApiError)) return "构件台账操作失败，请稍后重试。";
  const messages: Record<string, string> = {
    component_inventory_not_found: "这座桥还没有构件台账，请先生成初始台账。",
    component_inventory_conflict: "台账已变化或存在重复编号，请刷新后检查。",
    component_is_referenced: "该构件已被病害或正式项目引用，只能停用。",
    component_inventory_confirmation_blocked: "台账仍有未解决项，暂时不能确认。",
    standard_package_unavailable: "所选技术评定规范包当前不可用。",
  };
  return messages[error.code] ?? error.message;
}
