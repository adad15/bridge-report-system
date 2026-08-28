// 构件解析与评分树解析的接口客户端。
//
// 路由前缀沿用现网的 `/api/import-records/{id}/…`；编辑锁走 `X-Edit-Lock-Token` 头；
// 来源草稿并发版本走标准 `If-Match: "draft-<n>"`。这些约定与既有绑定链路一致，
// 不另起一套。
//
// 这里**不**复制任何后端规则：候选是否合法、批量替换命中几行、区间能不能展开、
// 评分树结果是否失效，全部由后端算好随响应下发。

import { ApiError, request } from "./apiClient";
import type { ResolutionWorkspaceResponse } from "../review/resolutionIndex";

/** 台账版本已变化：沿用现网既有错误码，前端已有处理分支。 */
export const INVENTORY_REVISION_CHANGED = "component_inventory_revision_changed";
/** 该桥尚无已确认台账版本：绑定类动作置灰，其余校对照常。 */
export const INVENTORY_NOT_CONFIRMED = "component_inventory_not_confirmed";
export const RESOLUTION_VERSION_CONFLICT = "resolution_version_conflict";
export const DRAFT_VERSION_CONFLICT = "review_draft_version_conflict";
export const PLAN_EXPIRED = "resolution_plan_expired";
export const PLAN_INVALIDATED = "resolution_plan_invalidated";

export interface WorkspaceComponentSummary {
  bridge_component_id: string;
  component_number: string;
  site_component_type: string;
  site_name: string;
  /** 由组所钉的台账版本当场派生（§17.2），不再冻在草稿里。 */
  standard_component_category_id: string;
  standard_bridge_type_id: string;
}

/**
 * "两侧"整体绑定候选。报告里写一条"两侧栏杆"，台账里却是左右各一件；
 * 后端把那一对找好并拼好文案，前端原样显示、一下绑两件。
 */
export interface WorkspaceSidePairOption {
  label: string;
  bridge_component_ids: string[];
}

export interface WorkspaceRatingResolution {
  present: boolean;
  status: string | null;
  rating_tree_node_id: string | null;
  match_method: string | null;
  version: number;
  content_changed_after_manual_resolution: boolean;
}

export interface WorkspaceDefectInstance {
  resolved_defect_instance_id: string;
  target_id: string;
  bridge_component_id: string;
  instance_order: number;
  instance_status: "active" | "ignored";
  is_photo_owner: boolean;
  version: number;
  component_resolution_version: number;
  overridden_fields: string[];
  effective_facts: Record<string, unknown>;
  rating_resolution: WorkspaceRatingResolution;
}

export interface WorkspaceGroupMember {
  member_id: string;
  source_candidate_id: string;
  source_order: number;
  instances: WorkspaceDefectInstance[];
}

export interface WorkspaceComponentGroup {
  group_id: string;
  source_component_name: string;
  source_component_number: string | null;
  normalized_component_number: string;
  resolution_mode: string;
  status: "unresolved" | "bound" | "missing";
  match_method: string | null;
  inventory_revision_id: string | null;
  version: number;
  /** 派生标签，后端算好；前端不按候选数再算一遍。 */
  ambiguous: boolean;
  split_eligible: boolean;
  split_expanded_count: number | null;
  /** 仅尚未解决的组会带；不成立时为 null。 */
  side_pair_option: WorkspaceSidePairOption | null;
  targets: WorkspaceComponentSummary[];
  candidates: WorkspaceComponentSummary[];
  members: WorkspaceGroupMember[];
  /** 允许的动作由后端判定；前端只按它决定按钮可用性。 */
  allowed_actions: string[];
  blocked_reasons: string[];
}

export interface WorkspacePartSummary {
  source_component_name: string;
  total: number;
  bound: number;
  unresolved: number;
  ambiguous: number;
  missing: number;
  group_ids: string[];
}

export interface WorkspaceProgress {
  group_count: number;
  bound_count: number;
  unresolved_count: number;
  ambiguous_count: number;
  missing_count: number;
  instance_count: number;
  active_instance_count: number;
  rating_matched_count: number;
  rating_unresolved_count: number;
  rating_missing_count: number;
}

export interface ResolutionWorkspace extends ResolutionWorkspaceResponse {
  import_record_id: string;
  bridge_id: string;
  draft_version: number;
  inventory_confirmed: boolean;
  inventory_revision_id: string | null;
  rating_tree: {
    version_id: string;
    tree_name: string;
    package_version: string;
    h21_package_version: string;
    maintenance_package_version: string;
  } | null;
  groups: WorkspaceComponentGroup[];
  parts: WorkspacePartSummary[];
  progress: WorkspaceProgress;
}

export interface ResolutionCommandResult {
  affected_groups: WorkspaceComponentGroup[];
  progress: WorkspaceProgress;
}

export interface ResolutionPlanRow {
  group_id: string;
  source_component_name: string;
  source_component_number: string;
  member_count: number;
  resolved_numbers: string[];
  target_component_ids: string[];
  outcome: "will_bind" | "will_clear" | "will_repoint" | "skipped" | "blocked";
  reason_code: string;
  reason_message: string;
}

export interface ResolutionPlanPreview {
  plan_token: string;
  operation_type: string;
  expires_at: string;
  will_apply_count: number;
  skipped_count: number;
  blocked_count: number;
  instances_before: number;
  instances_after: number;
  rating_recomputed_count: number;
  inventory_revision_id: string | null;
  rating_tree_version_id: string | null;
  rows: ResolutionPlanRow[];
}

function base(baseUrl: string, importId: string): string {
  return `${baseUrl}/api/import-records/${encodeURIComponent(importId)}`;
}

/**
 * lockToken 是必填参数而不是可选字段——可选的话，漏传的调用点会安静地拿到 409，
 * 而不是在类型检查时就被挡下来。与既有绑定接口同一取舍。
 */
function locked(method: string, body: unknown, lockToken: string): RequestInit {
  return {
    method,
    headers: { "Content-Type": "application/json", "X-Edit-Lock-Token": lockToken },
    body: JSON.stringify(body),
  };
}

export async function fetchResolutionWorkspace(
  baseUrl: string,
  importId: string
): Promise<ResolutionWorkspace> {
  return request<ResolutionWorkspace>(`${base(baseUrl, importId)}/resolution-workspace`);
}

export async function applyComponentResolution(
  baseUrl: string,
  importId: string,
  groupId: string,
  input: {
    expected_version: number;
    action: "bind" | "mark_missing" | "clear";
    targets?: { bridge_component_id: string; target_role?: string }[];
    expected_inventory_revision_id?: string;
  },
  lockToken: string
): Promise<ResolutionCommandResult> {
  return request<ResolutionCommandResult>(
    `${base(baseUrl, importId)}/component-groups/${encodeURIComponent(groupId)}/resolution`,
    locked("PUT", input, lockToken)
  );
}

export async function applyRatingResolution(
  baseUrl: string,
  importId: string,
  instanceId: string,
  input: {
    expected_version: number;
    rating_tree_node_id: string;
    expected_rating_tree_version_id?: string;
  },
  lockToken: string
): Promise<ResolutionCommandResult> {
  return request<ResolutionCommandResult>(
    `${base(baseUrl, importId)}/defect-instances/${encodeURIComponent(instanceId)}/rating-resolution`,
    locked("PUT", input, lockToken)
  );
}

export async function applyFactOverrides(
  baseUrl: string,
  importId: string,
  instanceId: string,
  input: {
    expected_version: number;
    overrides?: Record<string, unknown>;
    /** 清除是删键，不是写 null：两者在接口上必须分得开。 */
    cleared_fields?: string[];
  },
  lockToken: string
): Promise<ResolutionCommandResult> {
  return request<ResolutionCommandResult>(
    `${base(baseUrl, importId)}/defect-instances/${encodeURIComponent(instanceId)}/fact-overrides`,
    locked("PUT", input, lockToken)
  );
}

export async function applyInstanceStatus(
  baseUrl: string,
  importId: string,
  instanceId: string,
  input: { expected_version: number; instance_status: "active" | "ignored" },
  lockToken: string
): Promise<ResolutionCommandResult> {
  return request<ResolutionCommandResult>(
    `${base(baseUrl, importId)}/defect-instances/${encodeURIComponent(instanceId)}/status`,
    locked("PUT", input, lockToken)
  );
}

export interface ManualDefectResponse {
  source_defect: Record<string, unknown>;
  draft_version: number;
  result: ResolutionCommandResult;
}

/**
 * 手工新增病害。来源事实与解析状态在后端同一事务里写入。
 *
 * 响应里的 `source_defect` 与 `draft_version` 必须**一起**并进本地状态：只更新版本却
 * 保留缺少新候选的旧草稿，下一次整份保存就会把它当成"用户删掉了"。
 */
export async function addManualDefect(
  baseUrl: string,
  importId: string,
  input: {
    bridge_component_id: string;
    rating_tree_node_id: string;
    defect_facts: Record<string, unknown>;
    expected_inventory_revision_id?: string;
  },
  draftVersion: number,
  lockToken: string
): Promise<ManualDefectResponse> {
  return request<ManualDefectResponse>(`${base(baseUrl, importId)}/manual-defects`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "X-Edit-Lock-Token": lockToken,
      "If-Match": `"draft-${draftVersion}"`,
    },
    body: JSON.stringify(input),
  });
}

export type ResolutionPlanIntent =
  | { operation_type: "bulk_replace"; source_component_name?: string; find: string; replace: string }
  | { operation_type: "range_expand"; group_ids: string[] }
  | { operation_type: "inventory_repoint"; group_ids: string[] };

/** 预览由后端生成；前端执行时只提交 plan token，不提交自己算出来的结果集合。 */
export async function createResolutionPlan(
  baseUrl: string,
  importId: string,
  intent: ResolutionPlanIntent & { expected_inventory_revision_id?: string },
  lockToken: string
): Promise<ResolutionPlanPreview> {
  return request<ResolutionPlanPreview>(
    `${base(baseUrl, importId)}/resolution-plans`,
    locked("POST", intent, lockToken)
  );
}

export async function applyResolutionPlan(
  baseUrl: string,
  importId: string,
  planToken: string,
  lockToken: string
): Promise<{ apply_result: Record<string, unknown>; result: ResolutionCommandResult | null }> {
  return request(
    `${base(baseUrl, importId)}/resolution-plans/${encodeURIComponent(planToken)}/apply`,
    locked("POST", {}, lockToken)
  );
}

export { ApiError };
