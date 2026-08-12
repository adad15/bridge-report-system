// 与领域无关的通用 HTTP 客户端底座：ApiError / parseError / request<T>。
// navigationApi.ts 与 reviewApi.ts 都从这里导入；本文件不依赖任何契约/领域类型。

export interface ApiErrorIssue {
  path: string;
  message: string;
}

/**
 * 所有非 2xx 响应（以及无法解析的成功响应体）统一抛出的错误类型。
 *
 * 后端错误体一般是 {code, message}，400 契约校验额外带 issues。少数端点（POST
 * .../confirm 在 can_confirm=false 时）会把完整的 PreflightReport（没有 code 字段）
 * 直接当作 409 响应体返回：parseError 对任何缺 code 的响应体都退化为中性 code
 * "unrecognized_error_response" 并把原始响应体存进 details。需要 preflight 语义的
 * 调用方（confirmImport）自行按 details 形状把 code 重标为 "preflight_failed"，
 * 避免通用底座把无关失败误标成 preflight 专属错误。
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
 * - 响应体没有 code 字段（PreflightReport 当 409 body、代理错误页、后端崩溃等）：
 *   退化为中性 synthetic code "unrecognized_error_response"，并把整个响应体存进
 *   details，避免调用方拿到 code=undefined，也避免把无关失败误标成某个业务专属 code。
 */
export async function parseError(response: Response): Promise<ApiError> {
  const body = await readJsonBody(response);

  if (isRecord(body) && typeof body.code === "string") {
    const rawMessage = typeof body.message === "string" ? body.message.trim() : "";
    const message = rawMessage || `请求失败（HTTP ${response.status}，错误码 ${body.code}）`;
    const issues = Array.isArray(body.issues) ? (body.issues as ApiErrorIssue[]) : undefined;
    return new ApiError(body.code, message, { issues, details: body });
  }

  return new ApiError("unrecognized_error_response", `请求失败（HTTP ${response.status}）`, { details: body });
}

// 模块级会话令牌：由 AuthContext 在登录/登出/会话恢复时设置。放在这里（而不是
// 每个领域 API 手动传 token）保证所有请求自动携带 Authorization，apiClient 仍不依赖 React。
let authToken: string | null = null;

export function setAuthToken(token: string | null): void {
  authToken = token;
}

// 401 处理器：AuthContext 注册，用于"会话过期/被清除"时清空本地状态回登录页。
// 仅在请求确实带了 token 时触发——未登录状态的 401（如登录接口的密码错误）不触发，
// 避免登录页自身的失败反馈被全局登出逻辑吞掉。
let unauthorizedHandler: (() => void) | null = null;

export function setUnauthorizedHandler(handler: (() => void) | null): void {
  unauthorizedHandler = handler;
}

/**
 * 统一的 fetch 包装：发请求、非 2xx 时 throw ApiError，否则返回解析后的 JSON。
 * 成功响应体解析失败（例如后端返回了非 JSON）也抛 ApiError（code "invalid_response_body"），
 * 而不是把裸 SyntaxError 抛给调用方——与 readJsonBody 的 try/catch 对称。
 */
export async function request<T>(url: string, init?: RequestInit): Promise<T> {
  const hadToken = authToken !== null;
  let effectiveInit = init;
  if (hadToken) {
    const headers = new Headers(init?.headers);
    if (!headers.has("Authorization")) {
      headers.set("Authorization", `Bearer ${authToken}`);
    }
    effectiveInit = { ...init, headers };
  }
  const response = effectiveInit === undefined ? await fetch(url) : await fetch(url, effectiveInit);
  if (!response.ok) {
    if (response.status === 401 && hadToken) {
      unauthorizedHandler?.();
    }
    throw await parseError(response);
  }
  try {
    return (await response.json()) as T;
  } catch {
    throw new ApiError("invalid_response_body", `响应体不是合法的 JSON（HTTP ${response.status}）。`);
  }
}
