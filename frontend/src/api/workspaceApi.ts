import { ApiError, request } from "./apiClient";

const JSON_HEADERS = { "Content-Type": "application/json" };

export interface WorkspaceBridge {
  id: string;
  system_number: string;
  bridge_name: string;
  route_name: string | null;
  status: string;
}

export interface WorkspaceInspection {
  id: string;
  system_number: string;
  inspection_year: number;
  status: string;
  version_number: number;
  is_current: boolean;
  overall_score: number | null;
  overall_grade: string | null;
  created_at: string | null;
  updated_at: string | null;
}

export interface WorkspaceStatistics {
  defect_count: number;
  photo_count: number;
  rating_item_count: number;
  pending_count: number;
  confirmed_count: number;
  modified_count: number;
  ignored_count: number;
  object_warning_count: number;
}

export type WorkspaceImportAction = "parse" | "continue_review" | "view_result" | "none";

export interface WorkspaceImport {
  id: string;
  system_number: string;
  import_name: string;
  source_type: string;
  import_status: string;
  importer_name: string | null;
  created_at: string | null;
  updated_at: string | null;
  statistics: WorkspaceStatistics;
  edit_lock: {
    owner_username: string;
    owner_display_name: string;
    acquired_at: string;
    expires_at: string;
  } | null;
  available_action: WorkspaceImportAction;
}

export interface WorkspacePendingSummary {
  import_count: number;
  unbound_observation_count: number;
  total_count: number;
}

export interface BridgeOverview {
  bridge: WorkspaceBridge;
  latest_inspection: WorkspaceInspection | null;
  recent_inspections: WorkspaceInspection[];
  structure_ratings: Array<{
    rating_level: string;
    rating_item_name: string;
    score: number | null;
    grade: string | null;
  }>;
  pending: WorkspacePendingSummary;
  defect_archive: {
    component_count: number;
    thread_count: number;
    unbound_observation_count: number;
  };
}

export interface InspectionWorkspace {
  bridge: WorkspaceBridge;
  inspection_year: WorkspaceInspection;
  imports: WorkspaceImport[];
  pending: WorkspacePendingSummary;
}

export interface InspectionYearDeletionImpact {
  bridge: { id: string; system_number: string; bridge_name: string };
  inspection_year: number;
  version_numbers: number[];
  counts: {
    inspection_versions: number;
    import_records: number;
    defect_observations: number;
    defect_measurements: number;
    defect_photos: number;
    condition_ratings: number;
    archived_files_to_delete: number;
    shared_files_retained: number;
    defect_threads_affected: number;
    defect_comparisons: number;
  };
  active_edit_locks: Array<{
    import_record_id: string;
    owner_username: string;
    owner_display_name: string;
    acquired_at: string;
    expires_at: string;
  }>;
  confirmation_text: string;
  impact_token: string;
}

export interface DeleteInspectionYearResult {
  deleted: true;
  deletion_audit_id: string;
  bridge_id: string;
  inspection_year: number;
  deleted_counts: InspectionYearDeletionImpact["counts"];
  next_inspection_year_id: string | null;
  file_cleanup: { completed: number; failed: number };
}

export async function fetchBridgeOverview(baseUrl: string, bridgeId: string): Promise<BridgeOverview> {
  return request(`${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/overview`);
}

export async function fetchInspectionWorkspace(
  baseUrl: string,
  inspectionYearId: string
): Promise<InspectionWorkspace> {
  return request(`${baseUrl}/api/inspection-years/${encodeURIComponent(inspectionYearId)}/workspace`);
}

export async function fetchInspectionYearDeletionImpact(
  baseUrl: string,
  inspectionYearId: string
): Promise<InspectionYearDeletionImpact> {
  return request(`${baseUrl}/api/inspection-years/${encodeURIComponent(inspectionYearId)}/deletion-impact`);
}

export async function deleteInspectionYear(
  baseUrl: string,
  inspectionYearId: string,
  input: { impact_token: string; confirmation_text: string; reason: string }
): Promise<DeleteInspectionYearResult> {
  return request(`${baseUrl}/api/inspection-years/${encodeURIComponent(inspectionYearId)}`, {
    method: "DELETE",
    headers: JSON_HEADERS,
    body: JSON.stringify(input),
  });
}

export async function createInspectionYear(
  baseUrl: string,
  bridgeId: string,
  inspectionYear: number
): Promise<WorkspaceInspection> {
  const body = await request<{ inspection_year: WorkspaceInspection }>(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/inspection-years`,
    { method: "POST", headers: JSON_HEADERS, body: JSON.stringify({ inspection_year: inspectionYear }) }
  );
  return body.inspection_year;
}

export async function uploadWordImport(
  baseUrl: string,
  inspectionYearId: string,
  file: File,
  sourceType: "软件导出Word" | "正式Word"
): Promise<WorkspaceImport> {
  const form = new FormData();
  form.append("file", file);
  form.append("source_type", sourceType);
  const body = await request<{ import_record: WorkspaceImport }>(
    `${baseUrl}/api/inspection-years/${encodeURIComponent(inspectionYearId)}/import-records/word`,
    { method: "POST", body: form }
  );
  return body.import_record;
}

export function workspaceErrorMessage(error: unknown): string {
  if (!(error instanceof ApiError)) return "操作失败，请稍后重试。";
  const stable: Record<string, string> = {
    bridge_not_found: "桥梁不存在或已被删除。",
    inspection_year_not_found: "年度检测不存在或已被删除。",
    inspection_year_not_current: "该年度已不是当前版本，不能继续导入资料。",
    inspection_year_already_exists: "该年度已经存在，将进入已有年度。",
    invalid_word_file: "请选择一个非空的 .docx 文件。",
    word_file_too_large: "Word 文件超过允许的上传大小。",
    word_archive_failed: "Word 文件归档失败，请重试。",
    inspection_year_edit_locked: "该年度仍有人正在编辑，暂时不能删除。",
    deletion_impact_changed: "删除影响范围已经变化，请重新核对后再次确认。",
    deletion_reason_required: "请填写删除原因。",
  };
  return stable[error.code] ?? error.message;
}
