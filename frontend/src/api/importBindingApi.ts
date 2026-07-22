import { request } from "./apiClient";

export type ComponentBindingStatus = "bound" | "unmatched" | "ambiguous" | "missing";

export interface BindingRow {
  component_number: string;
  defect_count: number;
  status: ComponentBindingStatus;
  bridge_component_id: string | null;
  candidate_component_ids: string[];
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
  groups: BindingGroup[];
}

export interface BindingTarget {
  part_name: string;
  component_number: string;
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

export function bindComponent(
  baseUrl: string,
  importId: string,
  input: BindingTarget & { bridge_component_id: string }
) {
  return overviewRequest(bindingUrl(baseUrl, importId, "/bind"), json("POST", input));
}

export function markComponentMissing(baseUrl: string, importId: string, input: BindingTarget) {
  return overviewRequest(bindingUrl(baseUrl, importId, "/mark-missing"), json("POST", input));
}

export function clearComponentBinding(baseUrl: string, importId: string, input: BindingTarget) {
  return overviewRequest(bindingUrl(baseUrl, importId, "/clear"), json("POST", input));
}
