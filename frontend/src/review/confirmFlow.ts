import type { ConfirmResponse, PreflightResponse } from "../api/reviewApi";

// 确认入库按钮解锁逻辑（模块 05 §7.5/§9.3）：必须先跑过一次入库前检查
// （POST .../preflight-confirm）且返回 can_confirm=true，才允许点击"确认年度事实入库"。
// preflight 为 null 表示还没跑过检查，或者草稿在上一次检查之后又被编辑过——调用方
// （ReviewWorkspacePage 的 dispatchDraft）在任何编辑 action 之后都会把 preflight 重置回
// null，逼用户对最新草稿重新跑一次检查，避免拿着过期的 can_confirm=true 结果入库。
export function canPressConfirm(preflight: PreflightResponse | null): boolean {
  return preflight !== null && preflight.can_confirm;
}

// 入库前检查按钮是否可用（模块 05 §6.1）：入库前检查 / 确认入库端点只读数据库里已保存的
// parsed_result_json，不读内存草稿。因此草稿有未保存修改（dirty）时必须禁用入库前检查，逼
// 用户先"保存草稿"，否则会对着旧的已保存数据跑检查、误以为通过。busy/readOnly 时同样禁用。
export function canRunPreflight(dirty: boolean, busy: boolean, readOnly: boolean): boolean {
  return !dirty && !busy && !readOnly;
}

export interface RevisionFormValidation {
  valid: boolean;
  error?: string;
}

// 修订版确认弹窗的必填校验（模块 05 §6.4/§10.4）：同桥同年已有当前有效事实时，
// 必须显式勾选"作为修订版确认"并填写修订说明，两者缺一都要提示具体缺了哪个字段，
// 而不是一句笼统的"请完整填写"，减少用户来回猜是勾选框还是说明没填。
export function validateRevisionForm(checked: boolean, note: string): RevisionFormValidation {
  if (!checked) {
    return { valid: false, error: "请先勾选“作为修订版确认”。" };
  }
  if (note.trim().length === 0) {
    return { valid: false, error: "请填写修订说明。" };
  }
  return { valid: true };
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

// 把 confirm 端点 409（can_confirm=false）时 ApiError.details 里的原始响应体，安全地
// 收窄成 PreflightResponse，用于把最新的阻断错误刷新回入库前检查结果面板（模块 05 §13）。
// 只做形状校验，不做深度契约校验——这是受信任后端返回的错误体，只是这次走的是 409
// 路径而不是 POST .../preflight-confirm 的 200 路径，形状应当与 PreflightResponse 一致。
export function parsePreflightDetails(details: unknown): PreflightResponse | null {
  if (!isRecord(details)) {
    return null;
  }
  if (typeof details.can_confirm !== "boolean") return null;
  if (typeof details.requires_revision_confirmation !== "boolean") return null;
  if (!Array.isArray(details.blocking_errors) || !Array.isArray(details.warnings)) return null;
  return details as unknown as PreflightResponse;
}

export function formatConfirmSuccess(result: ConfirmResponse): string {
  return `已确认系统评定：病害 ${result.written.defect_observations}、尺寸 ${result.written.defect_measurements}、` +
    `照片 ${result.written.defect_photos}；系统评分投影 ${result.written.condition_ratings}、` +
    `构件结果 ${result.written.assessment_component_results}、评定层级 ${result.written.assessment_part_results}、` +
    `计算轨迹 ${result.written.assessment_rule_traces}；年度版本 v${result.version_number}。`;
}
