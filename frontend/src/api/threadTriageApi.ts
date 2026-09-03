import { request } from "./apiClient";

// 线索整理工作台 API。
//
// 与旧整理页最要紧的差别：**首屏只发一个请求**。旧页面为每条未绑定观测各发一次候选请求，
// 百股大桥 1197 条就是 1197 个并发请求，浏览器每域名 6 条连接，一张卡都加载不完。
// 这里的摘要一次给出全部批次与异常簇，明细等用户展开某个批次时才取。

export interface TriageSampleGroup {
  group_id: string;
  business_component_code: string | null;
  years: number[];
  target_thread_id: string | null;
}

export interface TriageBatchSummary {
  batch_id: string;
  fingerprint: string;
  action: "create" | "bind";
  structure_part: string;
  component_type: string;
  defect_type: string;
  defect_location: string | null;
  year_set: number[];
  group_count: number;
  observation_count: number;
  /** 头、中、尾各一条，稳定不随刷新变化。 */
  sample_groups: TriageSampleGroup[];
}

export interface TriageOverlapTarget {
  kind: "group" | "thread";
  id: string;
  display_name: string | null;
  system_number: string | null;
  normalized_location: string | null;
}

export interface TriageManualObservation {
  id: string;
  inspection_year: number;
  defect_type: string;
  defect_location: string | null;
  updated_at: string;
  /**
   * 展示字段：判断"这几条是不是同一处病害"的实际依据——标度看恶化趋势、尺寸看连续性、
   * 照片是最终判据。后端随异常簇一起给；标成可选是为了让旧后端也能降级渲染。
   */
  system_number?: string;
  scale?: string | null;
  defect_description?: string;
  measurements?: string[];
  photos?: Array<{ id: string; photo_number: string }>;
}

export interface TriageManualGroup {
  group_id: string;
  bridge_component_id: string;
  business_component_code: string | null;
  defect_type: string | null;
  defect_location: string | null;
  target_thread_id: string | null;
  observations: TriageManualObservation[];
}

export interface TriageManualCluster {
  cluster_id: string;
  reason_codes: string[];
  group_count: number;
  observation_count: number;
  groups: TriageManualGroup[];
  overlap_targets: TriageOverlapTarget[];
  related_threads: Array<{
    id: string;
    system_number: string;
    thread_name: string;
    bridge_component_id: string;
    defect_type: string;
    defect_location: string | null;
  }>;
}

export interface TriageSummary {
  snapshot_id: string;
  unbound_observation_count: number;
  batchable_group_count: number;
  batchable_observation_count: number;
  manual_group_count: number;
  manual_observation_count: number;
  batches: TriageBatchSummary[];
  manual_clusters: TriageManualCluster[];
}

/** 批次明细与异常簇现在是同一份形状：展示字段由同一条后端取数路径供给。 */
export type TriageDetailObservation = TriageManualObservation;

export interface TriageDetailGroup {
  group_id: string;
  bridge_component_id: string;
  business_component_code: string | null;
  /** bind 批次逐组各有各的目标；批次层面不存在单一线索。 */
  target_thread: { thread_id: string; system_number: string | null; thread_name: string | null } | null;
  observations: TriageDetailObservation[];
}

export interface TriageBatchDetail {
  snapshot_id: string;
  batch_id: string;
  fingerprint: string;
  action: "create" | "bind";
  structure_part: string;
  component_type: string;
  defect_type: string;
  defect_location: string | null;
  year_set: number[];
  group_count: number;
  observation_count: number;
  groups: TriageDetailGroup[];
}

export interface TriageApplyResult {
  group_id: string;
  bridge_component_id: string;
  thread_id: string;
  thread_system_number: string;
  outcome: "created" | "bound" | "already_completed";
}

export interface TriageApplyResponse {
  /** already_completed 是成功：你要的状态已经达成。 */
  status: "applied" | "already_completed";
  groups_applied: number;
  threads_created: number;
  observations_bound: number;
  results: TriageApplyResult[];
}

export interface TriageApplyIssue {
  reason_code: string;
  message: string;
  group_id: string | null;
  bridge_component_id: string | null;
  observation_id: string | null;
}

export function fetchTriageSummary(
  baseUrl: string, bridgeId: string, signal?: AbortSignal,
): Promise<TriageSummary> {
  return request<TriageSummary>(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/thread-triage`, { signal });
}

export function fetchTriageBatchDetail(
  baseUrl: string, bridgeId: string, batchId: string, signal?: AbortSignal,
): Promise<TriageBatchDetail> {
  return request<TriageBatchDetail>(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/thread-triage/batches/${
      encodeURIComponent(batchId)}`, { signal });
}

export interface TriageApplyPayload {
  batch_id: string;
  batch_fingerprint: string;
  action: "create" | "bind";
  idempotency_key?: string;
  groups: Array<{
    group_id: string;
    target_thread_id?: string;
    observations: Array<{ id: string; updated_at: string }>;
  }>;
}

/**
 * 从批次明细与"被剔除的组"构造提交负载。
 *
 * 剔掉的组不出现在负载里——服务端仍会自己重算组的构件、类型与匹配，这份清单只表达
 * "用户选了哪些"。
 */
export function buildTriageApplyPayload(
  detail: TriageBatchDetail, excludedGroupIds: ReadonlySet<string>,
): TriageApplyPayload {
  return {
    batch_id: detail.batch_id,
    batch_fingerprint: detail.fingerprint,
    action: detail.action,
    groups: detail.groups
      .filter((group) => !excludedGroupIds.has(group.group_id))
      .map((group) => ({
        group_id: group.group_id,
        ...(detail.action === "bind" && group.target_thread
          ? { target_thread_id: group.target_thread.thread_id }
          : {}),
        observations: group.observations.map((observation) => ({
          id: observation.id,
          updated_at: observation.updated_at,
        })),
      })),
  };
}

export function applyTriageBatch(
  baseUrl: string, bridgeId: string, payload: TriageApplyPayload,
): Promise<TriageApplyResponse> {
  return request<TriageApplyResponse>(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/thread-triage/apply`,
    {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    },
  );
}

export interface TriageResolvePayload {
  action: "create" | "bind";
  bridge_component_id: string;
  /** create 时由人选定，不是从任何一条观测抄的——合并之所以需要人，就是因为没有哪条写法天然权威。 */
  defect_type?: string;
  defect_location?: string;
  target_thread_id?: string;
  /** 选中观测的位置或类型不一致时必须为 true，否则服务端拒绝。 */
  confirm_inexact_merge?: boolean;
  observations: Array<{ id: string; updated_at: string }>;
}

export function resolveTriageCluster(
  baseUrl: string, bridgeId: string, payload: TriageResolvePayload,
): Promise<TriageApplyResponse> {
  return request<TriageApplyResponse>(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/thread-triage/resolve`,
    {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    },
  );
}

/** 选中的观测是否跨越了不止一种"类型+位置"写法——跨了就得人显式担责。 */
export function needsInexactMergeConfirmation(
  observations: Array<{ defect_type: string; defect_location: string | null }>,
): boolean {
  const keys = new Set(
    observations.map((observation) =>
      `${observation.defect_type.trim()}\u001f${(observation.defect_location ?? "").trim()}`));
  return keys.size > 1;
}
