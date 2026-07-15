import { request } from "./apiClient";

// ApiError / parseError / request<T> 现居 apiClient.ts（与领域无关的通用 HTTP 底座）。
// 为兼容既有导入方，这里再导出 ApiError 与 ApiErrorIssue。
export { ApiError } from "./apiClient";
export type { ApiErrorIssue } from "./apiClient";

export interface BridgeSummary {
  id: string;
  system_number: string;
  bridge_name: string;
  route_name: string | null;
  status: string;
}

export interface InspectionYearSummary {
  id: string;
  system_number: string;
  inspection_year: number;
  status: string;
  version_number: number;
  is_current: boolean;
}

export interface ImportRecordSummary {
  id: string;
  system_number: string;
  import_name: string;
  source_type: string;
  import_status: string;
  inspection_year_id: string | null;
  importer_name: string | null;
  created_at: string;
  edit_lock: {
    owner_username: string;
    owner_display_name: string;
    acquired_at: string;
    expires_at: string;
  } | null;
}

export async function fetchBridges(baseUrl: string): Promise<BridgeSummary[]> {
  const body = await request<{ bridges: BridgeSummary[] }>(`${baseUrl}/api/bridges`);
  return body.bridges;
}

export async function fetchInspectionYears(baseUrl: string, bridgeId: string): Promise<InspectionYearSummary[]> {
  const body = await request<{ inspection_years: InspectionYearSummary[] }>(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/inspection-years`
  );
  return body.inspection_years;
}

export async function fetchImportRecords(baseUrl: string, bridgeId: string): Promise<ImportRecordSummary[]> {
  const body = await request<{ import_records: ImportRecordSummary[] }>(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/import-records`
  );
  return body.import_records;
}
