// 共享的 ApiError / parseError / request<T> 辅助函数放在本文件（navigationApi.ts 不依赖
// contracts/annualInspection.ts，是两个 API 客户端里更基础的一侧），reviewApi.ts 复用本文件的导出。

export interface ApiErrorIssue {
  path: string;
  message: string;
}

/**
 * 所有非 2xx 响应统一抛出的错误类型。后端错误体一般是 {code, message}，400 契约校验
 * 额外带 issues。POST .../confirm 在 can_confirm=false 时会把完整的 PreflightReport
 * （没有 code 字段）直接当作 409 响应体返回——遇到这种缺 code 的响应体时，parseError
 * 会合成一个 code（见下方注释）并把原始响应体存进 details，供 UI（Task 14）渲染
 * blocking_errors/warnings。
 */
export class ApiError extends Error {
  code: string;
  issues?: ApiErrorIssue[];
  details?: unknown;

  constructor(code: string, message: string, options?: { issues?: ApiErrorIssue[]; details?: unknown }) {
    super(message);
    this.name = "ApiError";
    this.code = code;
    this.issues = options?.issues;
    this.details = options?.details;
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

async function readJsonBody(response: Response): Promise<unknown> {
  try {
    return await response.json();
  } catch {
    return null;
  }
}

/**
 * 把一个非 2xx 的 fetch Response 解析为 ApiError。
 *
 * - 响应体是 {code, message, issues?} 形状（绝大多数后端错误）：直接映射。
 * - 响应体没有 code 字段（目前仅 POST .../confirm 在 can_confirm=false 时把完整
 *   PreflightReport 当 409 body 返回属于此类）：合成一个 synthetic code
 *   "preflight_failed"，并把整个响应体存进 details，避免调用方拿到 code=undefined。
 */
export async function parseError(response: Response): Promise<ApiError> {
  const body = await readJsonBody(response);

  if (isRecord(body) && typeof body.code === "string") {
    const message = typeof body.message === "string" ? body.message : `请求失败（HTTP ${response.status}）`;
    const issues = Array.isArray(body.issues) ? (body.issues as ApiErrorIssue[]) : undefined;
    return new ApiError(body.code, message, { issues });
  }

  return new ApiError("preflight_failed", `请求失败（HTTP ${response.status}）`, { details: body });
}

/** 统一的 fetch 包装：发请求、非 2xx 时 throw ApiError，否则返回解析后的 JSON。 */
export async function request<T>(url: string, init?: RequestInit): Promise<T> {
  const response = init === undefined ? await fetch(url) : await fetch(url, init);
  if (!response.ok) {
    throw await parseError(response);
  }
  return (await response.json()) as T;
}

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
