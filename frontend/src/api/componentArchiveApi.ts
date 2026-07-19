import { request } from "./apiClient";

// 模块 06 构件病害档案 API。主页面只读优先：除线索创建与绑定外全部为 GET；
// 照片一律经 defectPhotoContentUrl 构造受控内容接口地址，前端绝不接触归档绝对路径。

export interface ComponentSummary {
  id: string;
  system_number: string;
  structure_part: string;
  component_type: string;
  business_component_code: string;
  thread_count: number;
  unbound_count: number;
  first_seen_year: number;
  latest_seen_year: number;
  latest_score: number | null;
  latest_score_year: number | null;
}

export interface ArchiveComponent {
  id: string;
  bridge_id: string;
  system_number: string;
  structure_part: string;
  component_type: string;
  business_component_code: string;
  current_status: string;
}

export interface ComponentYearRating {
  inspection_year: number;
  score: number | null;
  source_score: number | null;
  calculated_score: number | null;
  score_validation_status: string | null;
  score_resolution_reason: string | null;
  calculation_details: { standard?: string; ordered_deductions?: number[]; rounding_scale?: number };
  has_validation_details: boolean;
}

export interface ArchivePhoto {
  id: string;
  photo_number: string;
  photo_title: string | null;
}

export interface ArchiveMeasurement {
  measurement_type: string;
  value_type: "single" | "range" | null;
  numeric_value: number | null;
  minimum_value: number | null;
  maximum_value: number | null;
  unit: string | null;
  is_approximate: boolean;
  raw_text: string;
}

export interface ArchiveObservation {
  id: string;
  system_number: string;
  inspection_year: number;
  defect_thread_id: string | null;
  defect_type: string;
  defect_location: string | null;
  scale: string | null;
  defect_deduction: number | null;
  defect_description: string;
  review_status: string;
  updated_at: string;
  measurements: ArchiveMeasurement[];
  photos: ArchivePhoto[];
}

export interface ArchiveThread {
  id: string;
  system_number: string;
  thread_name: string;
  defect_type: string;
  defect_location: string | null;
  current_status: string;
  confirmation_status: string;
  first_seen_year: number | null;
  latest_seen_year: number | null;
  observations: ArchiveObservation[];
}

export interface ComponentDefectArchive {
  component: ArchiveComponent;
  ratings: ComponentYearRating[];
  threads: ArchiveThread[];
  unbound_observations: ArchiveObservation[];
}

export interface RevisionGroup {
  inspection_year: number;
  version_number: number;
  inspection_status: string;
  superseded_by_version: number | null;
  observations: ArchiveObservation[];
}

export interface UnboundObservation extends ArchiveObservation {
  component: {
    id: string;
    system_number: string;
    structure_part: string;
    component_type: string;
    business_component_code: string;
  };
}

export interface ThreadSuggestion extends Omit<ArchiveThread, "observations"> {
  match_basis: {
    same_component: boolean;
    same_defect_type: boolean;
    location_exact: boolean;
    location_contains: boolean;
  };
  suggestion_score: number;
}

export interface ObservationEvidence {
  source_raw_cells: Record<string, unknown>;
  source_table_title: string | null;
  source_table_index: number | null;
  source_row_number: number | null;
  import_record_system_number: string | null;
  source_file_system_number: string | null;
  source_file_name: string | null;
  temporary_source_status: string | null;
  original_word_retained: boolean;
}

export interface CreateThreadRequestBody {
  bridge_component_id: string;
  defect_type: string;
  defect_location: string;
  first_observation_id: string;
  expected_observation_updated_at: string;
  thread_name?: string;
}

export interface BindThreadRequestBody {
  defect_thread_id: string | null;
  expected_observation_updated_at: string;
  confirm_rebind: boolean;
}

export interface ThreadBindingResponse {
  created?: boolean;
  bound?: boolean;
  defect_thread_id?: string | null;
  defect_thread?: Omit<ArchiveThread, "observations">;
  observation_id: string;
  observation_updated_at: string;
}

const JSON_HEADERS = { "Content-Type": "application/json" };

function normalizedBase(baseUrl: string): string {
  return baseUrl.replace(/\/+$/, "");
}

export function defectPhotoContentUrl(baseUrl: string, defectPhotoId: string): string {
  return `${normalizedBase(baseUrl)}/api/defect-photos/${encodeURIComponent(defectPhotoId)}/content`;
}

export async function fetchComponents(baseUrl: string, bridgeId: string): Promise<ComponentSummary[]> {
  const body = await request<{ components: ComponentSummary[] }>(
    `${normalizedBase(baseUrl)}/api/bridges/${encodeURIComponent(bridgeId)}/components`
  );
  return body.components;
}

export async function fetchComponentArchive(baseUrl: string, componentId: string): Promise<ComponentDefectArchive> {
  return request(`${normalizedBase(baseUrl)}/api/bridge-components/${encodeURIComponent(componentId)}/defect-archive`);
}

export async function fetchComponentRevisions(baseUrl: string, componentId: string): Promise<RevisionGroup[]> {
  const body = await request<{ revisions: RevisionGroup[] }>(
    `${normalizedBase(baseUrl)}/api/bridge-components/${encodeURIComponent(componentId)}/defect-archive/revisions`
  );
  return body.revisions;
}

export async function fetchUnboundObservations(baseUrl: string, bridgeId: string): Promise<UnboundObservation[]> {
  const body = await request<{ unbound_observations: UnboundObservation[] }>(
    `${normalizedBase(baseUrl)}/api/bridges/${encodeURIComponent(bridgeId)}/unbound-defect-observations`
  );
  return body.unbound_observations;
}

export async function fetchThreadSuggestions(
  baseUrl: string,
  observationId: string
): Promise<ThreadSuggestion[]> {
  const body = await request<{ suggestions: ThreadSuggestion[] }>(
    `${normalizedBase(baseUrl)}/api/defect-observations/${encodeURIComponent(observationId)}/thread-suggestions`
  );
  return body.suggestions;
}

export async function fetchObservationEvidence(
  baseUrl: string,
  observationId: string
): Promise<ObservationEvidence> {
  return request(
    `${normalizedBase(baseUrl)}/api/defect-observations/${encodeURIComponent(observationId)}/evidence`
  );
}

export async function createDefectThread(
  baseUrl: string,
  body: CreateThreadRequestBody
): Promise<ThreadBindingResponse> {
  return request(`${normalizedBase(baseUrl)}/api/defect-threads`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(body),
  });
}

export async function bindObservationThread(
  baseUrl: string,
  observationId: string,
  body: BindThreadRequestBody
): Promise<ThreadBindingResponse> {
  return request(
    `${normalizedBase(baseUrl)}/api/defect-observations/${encodeURIComponent(observationId)}/defect-thread`,
    {
      method: "PUT",
      headers: JSON_HEADERS,
      body: JSON.stringify(body),
    }
  );
}

// 线索绑定接口的稳定错误码 -> 用户可操作的中文提示（模块 06 规格 §11）。
export const THREAD_BINDING_ERROR_MESSAGES: Record<string, string> = {
  observation_revision_conflict: "该观测已被其他操作更新，请刷新页面后重试。",
  observation_not_current: "该观测属于旧修订版年度，不能修改线索绑定。",
  observation_not_formal: "该观测不是正式事实，不能绑定线索。",
  thread_component_mismatch: "目标线索与观测不属于同一构件，请重新选择。",
  rebind_confirmation_required: "该观测已绑定线索，请先勾选确认后再重新绑定。",
  observation_referenced_by_confirmed_comparison: "该观测已被人工确认的历史对比引用，须先撤销相关对比结论。",
  observation_already_bound: "该观测已绑定其他线索，请改用重新绑定。",
  thread_required_field_missing: "标准病害类型与标准详细位置为必填项。",
  defect_thread_not_found: "目标线索不存在，请刷新页面。",
  defect_observation_not_found: "观测不存在，请刷新页面。",
};

export function threadBindingErrorMessage(code: string, fallback: string): string {
  return THREAD_BINDING_ERROR_MESSAGES[code] ?? fallback;
}
