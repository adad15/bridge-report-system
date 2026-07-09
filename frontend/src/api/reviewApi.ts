import type { BridgeAnnualInspectionData } from "../contracts/annualInspection";
import { isBridgeAnnualInspectionData } from "../contracts/annualInspection";
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

export interface ReviewResponse {
  import_record: ReviewImportRecordSummary;
  bridge: ReviewBridgeSummary;
  inspection_year: ReviewInspectionYearSummary | null;
  parsed_result: BridgeAnnualInspectionData;
  statistics: ReviewStatistics;
  has_current_annual_facts: boolean;
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

function reviewRoute(importRecordId: string, suffix: string): string {
  return `/api/import-records/${encodeURIComponent(importRecordId)}${suffix}`;
}

/**
 * 拉取校对详情。parsed_result 驱动整个校对编辑器，因此额外用运行时守卫
 * isBridgeAnnualInspectionData 校验；不满足契约时抛出 ApiError（不是让编辑器
 * 拿到形状不对的数据往下渲染）。ReviewResponse 其余字段（import_record/bridge/
 * inspection_year/statistics）来自受信任的后端组装，只做结构类型标注，不重复校验。
 */
export async function fetchReview(baseUrl: string, importRecordId: string): Promise<ReviewResponse> {
  const body = await request<ReviewResponse>(`${baseUrl}${reviewRoute(importRecordId, "/review")}`);
  if (!isBridgeAnnualInspectionData(body.parsed_result)) {
    throw new ApiError("invalid_review_payload", "校对数据不符合 BridgeAnnualInspectionData 契约。");
  }
  return body;
}

export async function saveReviewDraft(
  baseUrl: string,
  importRecordId: string,
  data: BridgeAnnualInspectionData
): Promise<{ saved: boolean; import_status: string }> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/review-draft")}`, {
    method: "PUT",
    headers: JSON_HEADERS,
    body: JSON.stringify(data),
  });
}

// 无请求体：后端的 preflight-confirm 路由本来就不读取请求体，只读当前已保存的
// parsed_result_json 跑一遍入库前检查。
export async function runPreflight(baseUrl: string, importRecordId: string): Promise<PreflightResponse> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/preflight-confirm")}`, {
    method: "POST",
  });
}

// details 是不是一份 PreflightReport（can_confirm=false 时被后端当 409 body 返回）。
function looksLikePreflightReport(details: unknown): boolean {
  return typeof details === "object" && details !== null && "can_confirm" in details;
}

export async function confirmImport(
  baseUrl: string,
  importRecordId: string,
  body: ConfirmRequestBody
): Promise<ConfirmResponse> {
  try {
    return await request<ConfirmResponse>(`${baseUrl}${reviewRoute(importRecordId, "/confirm")}`, {
      method: "POST",
      headers: JSON_HEADERS,
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
export async function cancelImport(baseUrl: string, importRecordId: string): Promise<{ cancelled: boolean }> {
  return request(`${baseUrl}${reviewRoute(importRecordId, "/cancel")}`, {
    method: "POST",
  });
}
