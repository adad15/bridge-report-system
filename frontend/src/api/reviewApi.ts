import type { BridgeAnnualInspectionData } from "../contracts/annualInspection";
import {
  isBridgeAnnualInspectionData,
  isLegacyAnnualInspectionData12,
  projectVersionTwoForLegacyReview,
  versionTwoWireData,
} from "../contracts/annualInspection";
import { ApiError, request } from "./apiClient";

const JSON_HEADERS = { "Content-Type": "application/json" };

export interface ReviewImportRecordSummary {
  id: string;
  system_number: string;
  import_name: string;
  source_type: string;
  import_status: string;
  importer_name: string | null;
  importer_version: string | null;
  created_at: string;
  updated_at: string;
}

export interface ReviewBridgeSummary {
  id: string;
  system_number: string;
  bridge_name: string;
  route_name: string | null;
}

export interface ReviewInspectionYearSummary {
  id: string;
  system_number: string;
  inspection_year: number;
  status: string;
  version_number: number;
  is_current: boolean;
}

export interface ReviewStatistics {
  defect_count: number;
  photo_count: number;
  rating_item_count: number;
  pending_count: number;
  confirmed_count: number;
  modified_count: number;
  ignored_count: number;
  object_warning_count: number;
}

// native_2_0：原生 2.0 草稿；legacy_pending_reparse：待校对的 1.x 旧草稿，
// 只读展示并提示重新解析（不做内存补造）；legacy_read_only：旧版终态记录。
export type ContractCompatibility = "native_2_0" | "legacy_pending_reparse" | "legacy_read_only";

// 重开校对范围：warnings_only=仅带警告的病害可改（任何登录用户）；
// full=全部可改（仅管理员可发起）。
export type ReopenScope = "warnings_only" | "full";

// 重开校对现场（import_records 审计列）；非重开态为 null。
export interface ReviewReopenState {
  reopened_at: string;
  reopened_by_username: string;
  scope: ReopenScope;
}

export interface EditLockSummary {
  owner_username: string;
  owner_display_name: string;
  owned_by_current_user: boolean;
  acquired_at: string;
  expires_at: string;
}

export interface AcquireEditLockResponse {
  acquired: true;
  lock_token: string;
  heartbeat_interval_seconds: number;
  lock: EditLockSummary;
}

export interface ReviewResponse {
  import_record: ReviewImportRecordSummary;
  bridge: ReviewBridgeSummary;
  inspection_year: ReviewInspectionYearSummary | null;
  parsed_result: BridgeAnnualInspectionData;
  statistics: ReviewStatistics;
  has_current_annual_facts: boolean;
  contract_compatibility: ContractCompatibility;
  reopen: ReviewReopenState | null;
  edit_lock: EditLockSummary | null;
}

export interface PreflightIssue {
  code: string;
  message: string;
  target_candidate_id: string | null;
}

export interface PreflightResponse {
  can_confirm: boolean;
  requires_revision_confirmation: boolean;
  blocking_errors: PreflightIssue[];
  warnings: PreflightIssue[];
}

export interface ConfirmWrittenCounts {
  defect_observations: number;
  defect_measurements: number;
  defect_photos: number;
  condition_ratings: number;
}

export interface ConfirmResponse {
  confirmed: true;
  inspection_year_id: string;
  version_number: number;
  written: ConfirmWrittenCounts;
}

export interface ConfirmRequestBody {
  confirm_revision: boolean;
  confirmation_note: string;
}

export interface ParseWordImportRequest {
  rule_profile: "辽宁国省干线";
  import_mode: "已有桥年度导入";
  file_role: "当前年度检测资料";
  data_role: "当前年度";
  inspection_date: string;
  report_number: string;
  project_name: string;
}

export interface ParseWordImportResponse {
  parsed: true;
  temporary_photo_file_count: number;
  photo_candidate_count: number;
  archived_photo_count: number;
}

function reviewRoute(importRecordId: string, suffix: string): string {
  return `/api/import-records/${encodeURIComponent(importRecordId)}${suffix}`;
}

export async function parseWordImport(
  baseUrl: string,
  importRecordId: string,
  body: ParseWordImportRequest
): Promise<ParseWordImportResponse> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/parse-word")}`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(body),
  });
}

function lockHeaders(lockToken: string, includeJson = false): Headers {
  const headers = new Headers(includeJson ? JSON_HEADERS : undefined);
  headers.set("X-Edit-Lock-Token", lockToken);
  return headers;
}

export function photoContentUrl(baseUrl: string, importRecordId: string, photoCandidateId: string): string {
  const normalizedBaseUrl = baseUrl.replace(/\/+$/, "");
  return `${normalizedBaseUrl}/api/import-records/${encodeURIComponent(importRecordId)}/photos/${encodeURIComponent(photoCandidateId)}/content`;
}

/**
 * 拉取校对详情。parsed_result 驱动整个校对编辑器，因此额外用运行时守卫
 * isBridgeAnnualInspectionData 校验；不满足契约时抛出 ApiError（不是让编辑器
 * 拿到形状不对的数据往下渲染）。ReviewResponse 其余字段（import_record/bridge/
 * inspection_year/statistics）来自受信任的后端组装，只做结构类型标注，不重复校验。
 */
export async function fetchReview(baseUrl: string, importRecordId: string): Promise<ReviewResponse> {
  const body = await request<Omit<ReviewResponse, "parsed_result"> & { parsed_result: unknown }>(
    `${baseUrl}${reviewRoute(importRecordId, "/review")}`,
  );
  if (isBridgeAnnualInspectionData(body.parsed_result)) {
    return {
      ...body,
      parsed_result: projectVersionTwoForLegacyReview(body.parsed_result),
    };
  }
  if (
    body.contract_compatibility !== "native_2_0" &&
    isLegacyAnnualInspectionData12(body.parsed_result)
  ) {
    return body as ReviewResponse;
  }
  throw new ApiError("invalid_review_payload", "校对数据不符合 BridgeAnnualInspectionData 契约。");
}

export async function saveReviewDraft(
  baseUrl: string,
  importRecordId: string,
  data: BridgeAnnualInspectionData,
  lockToken: string
): Promise<{ saved: boolean; import_status: string }> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/review-draft")}`, {
    method: "PUT",
    headers: lockHeaders(lockToken, true),
    body: JSON.stringify(versionTwoWireData(data)),
  });
}

// 无请求体：后端的 preflight-confirm 路由本来就不读取请求体，只读当前已保存的
// parsed_result_json 跑一遍入库前检查。
export async function runPreflight(baseUrl: string, importRecordId: string, lockToken: string): Promise<PreflightResponse> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/preflight-confirm")}`, {
    method: "POST",
    headers: lockHeaders(lockToken),
  });
}

// details 是不是一份 PreflightReport（can_confirm=false 时被后端当 409 body 返回）。
function looksLikePreflightReport(details: unknown): boolean {
  return typeof details === "object" && details !== null && "can_confirm" in details;
}

export async function confirmImport(
  baseUrl: string,
  importRecordId: string,
  body: ConfirmRequestBody,
  lockToken: string
): Promise<ConfirmResponse> {
  try {
    return await request<ConfirmResponse>(`${baseUrl}${reviewRoute(importRecordId, "/confirm")}`, {
      method: "POST",
      headers: lockHeaders(lockToken, true),
      body: JSON.stringify(body),
    });
  } catch (error) {
    // 通用底座对缺 code 的错误体一律标 "unrecognized_error_response"；只有 confirm 端点
    // 真正会收到 PreflightReport 形状的 409 body（can_confirm=false）——这里按 details 形状
    // 把它重标为 preflight 专属 code，供 UI（Task 14）据此渲染 blocking_errors。details 原样保留。
    if (error instanceof ApiError && error.code === "unrecognized_error_response" && looksLikePreflightReport(error.details)) {
      error.code = "preflight_failed";
    }
    throw error;
  }
}

// 无请求体：后端的 cancel 路由不读取请求体。
export async function cancelImport(baseUrl: string, importRecordId: string, lockToken: string): Promise<{ cancelled: boolean }> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/cancel")}`, {
    method: "POST",
    headers: lockHeaders(lockToken),
  });
}

// 重开校对：已确认记录翻回待校对（后端快照草稿供「放弃修改」还原）。
// full 范围仅管理员可发起，后端双重校验。
export async function reopenImport(
  baseUrl: string,
  importRecordId: string,
  scope: ReopenScope
): Promise<{ reopened: boolean; scope: ReopenScope; import_status: string; lock_token: string; edit_lock: EditLockSummary }> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/reopen")}`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ scope }),
  });
}

// 放弃重开修改：草稿还原为重开时快照，状态翻回已确认。
export async function restoreReopenedImport(
  baseUrl: string,
  importRecordId: string,
  lockToken: string
): Promise<{ restored: boolean; import_status: string }> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/reopen-restore")}`, {
    method: "POST",
    headers: lockHeaders(lockToken),
  });
}


export async function acquireEditLock(baseUrl: string, importRecordId: string): Promise<AcquireEditLockResponse> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/edit-lock")}`, { method: "POST" });
}

export async function heartbeatEditLock(
  baseUrl: string,
  importRecordId: string,
  lockToken: string
): Promise<{ renewed: true; lock: EditLockSummary }> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/edit-lock/heartbeat")}`, {
    method: "POST",
    headers: lockHeaders(lockToken),
  });
}

export async function releaseEditLock(
  baseUrl: string,
  importRecordId: string,
  lockToken: string,
  keepalive = false
): Promise<{ released: boolean }> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/edit-lock")}`, {
    method: "DELETE",
    headers: lockHeaders(lockToken),
    keepalive,
  });
}

export async function forceReleaseEditLock(
  baseUrl: string,
  importRecordId: string,
  reason: string
): Promise<{ released: true }> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/edit-lock/force-release")}`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ reason }),
  });
}
