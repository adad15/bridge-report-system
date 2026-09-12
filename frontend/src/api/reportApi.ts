// 报告模块的前端接口层：模板库、人员库、设备库、年度报告配置与生成任务。
//
// 一处纪律：**下载一律走任务 ID，不拼服务器路径。** 后端刻意不把临时文件路径放进
// 接口（设计 §22），这里也就没有任何地方能拼出一条磁盘路径。

import { downloadFile, request } from "./apiClient";
import { backendBaseUrl } from "../config";

const JSON_HEADERS = { "Content-Type": "application/json" };

// ---------------------------------------------------------------------------
// 模板库（设计 §7.1、§21.1）
// ---------------------------------------------------------------------------

export interface TemplateIssue {
  code: string;
  message: string;
  severity: "error" | "warning";
  location?: string;
}

export interface TemplateValidationResult {
  status: "valid" | "invalid";
  issues: TemplateIssue[];
  /** 锚点在文档里的真实顺序，供「查看锚点及文档顺序」用。 */
  anchors_in_document_order: string[];
  placeholders_used: string[];
  fields_used: string[];
}

export interface ReportTemplate {
  id: string;
  template_code: string;
  template_name: string;
  description: string | null;
  contract_type: string;
  file_id: string;
  file_checksum: string;
  file_name: string;
  /** 即 template.json：编号格式与所需人员角色。 */
  contract_config: {
    table_number_formats?: Record<string, string>;
    required_personnel_roles?: string[];
  };
  validation_status: string;
  validation_result: TemplateValidationResult | Record<string, never>;
  is_enabled: boolean;
  is_default: boolean;
  updated_by_display_name: string | null;
  updated_at: string;
  /** 被年度报告配置引用的次数。 */
  usage_count: number;
  /** 被引用的模板只能停用，不能删（设计 §17.4）。 */
  can_delete: boolean;
  /** 默认模板不能直接停用——先把默认让给别的模板。 */
  can_disable: boolean;
}

export interface ReportTemplateMetadata {
  template_code: string;
  template_name: string;
  description?: string;
  contract_type?: string;
  contract_config?: ReportTemplate["contract_config"];
}

export async function fetchReportTemplates(): Promise<ReportTemplate[]> {
  const body = await request<{ templates: ReportTemplate[] }>(
    `${backendBaseUrl}/api/report/templates`,
  );
  return body.templates;
}

/** 上传一份新模板。校验不通过时后端不写库，这里抛 ApiError，明细在 details 里。 */
export async function uploadReportTemplate(
  file: File,
  metadata: ReportTemplateMetadata,
): Promise<ReportTemplate> {
  const form = new FormData();
  form.append("metadata", JSON.stringify(metadata));
  form.append("file", file);
  return request<ReportTemplate>(`${backendBaseUrl}/api/report/templates`, {
    method: "POST",
    body: form,
  });
}

/** 替换当前模板文件。元数据不变，只换文件。 */
export async function replaceReportTemplateFile(
  id: string,
  file: File,
): Promise<ReportTemplate> {
  const form = new FormData();
  form.append("file", file);
  return request<ReportTemplate>(
    `${backendBaseUrl}/api/report/templates/${encodeURIComponent(id)}/file`,
    { method: "POST", body: form },
  );
}

/** 下载模板源文件。走 fetch 而不是超链接：导航不带 Authorization 头，会 401。 */
export async function downloadReportTemplateFile(
  id: string,
  fallbackName: string,
): Promise<void> {
  await downloadFile(
    `${backendBaseUrl}/api/report/templates/${encodeURIComponent(id)}/file`,
    fallbackName,
  );
}

export async function setReportTemplateEnabled(
  id: string,
  isEnabled: boolean,
): Promise<ReportTemplate> {
  return request<ReportTemplate>(
    `${backendBaseUrl}/api/report/templates/${encodeURIComponent(id)}/enabled`,
    { method: "POST", headers: JSON_HEADERS, body: JSON.stringify({ is_enabled: isEnabled }) },
  );
}

export async function setReportTemplateDefault(id: string): Promise<ReportTemplate> {
  return request<ReportTemplate>(
    `${backendBaseUrl}/api/report/templates/${encodeURIComponent(id)}/default`,
    { method: "POST", headers: JSON_HEADERS, body: "{}" },
  );
}

export async function deleteReportTemplate(id: string): Promise<void> {
  await request<{ status: string }>(
    `${backendBaseUrl}/api/report/templates/${encodeURIComponent(id)}`,
    { method: "DELETE" },
  );
}

// ---------------------------------------------------------------------------
// 人员库与设备库（设计 §15、§21.2、§21.3）
// ---------------------------------------------------------------------------

export interface ReportPersonnel {
  id: string;
  full_name: string;
  organization: string | null;
  job_title: string | null;
  professional_title: string | null;
  qualification_certificate_no: string | null;
  phone: string | null;
  email: string | null;
  remarks: string | null;
  is_enabled: boolean;
  /** 被年度配置引用的次数；大于 0 时只能停用，不能删。 */
  assignment_count: number;
  updated_at: string;
}

export interface ReportPersonnelInput {
  full_name: string;
  organization?: string;
  job_title?: string;
  professional_title?: string;
  qualification_certificate_no?: string;
  phone?: string;
  email?: string;
  remarks?: string;
}

export interface ReportEquipment {
  id: string;
  equipment_name: string;
  model_spec: string | null;
  asset_number: string | null;
  measurement_range: string | null;
  accuracy: string | null;
  calibration_certificate_no: string | null;
  calibration_valid_until: string | null;
  remarks: string | null;
  is_enabled: boolean;
  assignment_count: number;
  updated_at: string;
}

export interface ReportEquipmentInput {
  equipment_name: string;
  model_spec?: string;
  asset_number?: string;
  measurement_range?: string;
  accuracy?: string;
  calibration_certificate_no?: string;
  calibration_valid_until?: string;
  remarks?: string;
}

export async function fetchReportPersonnel(onlyEnabled = false): Promise<ReportPersonnel[]> {
  const query = onlyEnabled ? "?only_enabled=1" : "";
  const body = await request<{ personnel: ReportPersonnel[] }>(
    `${backendBaseUrl}/api/report/personnel${query}`,
  );
  return body.personnel;
}

export async function createReportPersonnel(
  input: ReportPersonnelInput,
): Promise<ReportPersonnel> {
  return request<ReportPersonnel>(`${backendBaseUrl}/api/report/personnel`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(input),
  });
}

export async function updateReportPersonnel(
  id: string,
  input: ReportPersonnelInput,
): Promise<ReportPersonnel> {
  return request<ReportPersonnel>(
    `${backendBaseUrl}/api/report/personnel/${encodeURIComponent(id)}`,
    { method: "PUT", headers: JSON_HEADERS, body: JSON.stringify(input) },
  );
}

export async function setReportPersonnelEnabled(
  id: string,
  isEnabled: boolean,
): Promise<ReportPersonnel> {
  return request<ReportPersonnel>(
    `${backendBaseUrl}/api/report/personnel/${encodeURIComponent(id)}/enabled`,
    { method: "POST", headers: JSON_HEADERS, body: JSON.stringify({ is_enabled: isEnabled }) },
  );
}

export async function deleteReportPersonnel(id: string): Promise<void> {
  await request<{ status: string }>(
    `${backendBaseUrl}/api/report/personnel/${encodeURIComponent(id)}`,
    { method: "DELETE" },
  );
}

export async function fetchReportEquipment(onlyEnabled = false): Promise<ReportEquipment[]> {
  const query = onlyEnabled ? "?only_enabled=1" : "";
  const body = await request<{ equipment: ReportEquipment[] }>(
    `${backendBaseUrl}/api/report/equipment${query}`,
  );
  return body.equipment;
}

export async function createReportEquipment(
  input: ReportEquipmentInput,
): Promise<ReportEquipment> {
  return request<ReportEquipment>(`${backendBaseUrl}/api/report/equipment`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(input),
  });
}

export async function updateReportEquipment(
  id: string,
  input: ReportEquipmentInput,
): Promise<ReportEquipment> {
  return request<ReportEquipment>(
    `${backendBaseUrl}/api/report/equipment/${encodeURIComponent(id)}`,
    { method: "PUT", headers: JSON_HEADERS, body: JSON.stringify(input) },
  );
}

export async function setReportEquipmentEnabled(
  id: string,
  isEnabled: boolean,
): Promise<ReportEquipment> {
  return request<ReportEquipment>(
    `${backendBaseUrl}/api/report/equipment/${encodeURIComponent(id)}/enabled`,
    { method: "POST", headers: JSON_HEADERS, body: JSON.stringify({ is_enabled: isEnabled }) },
  );
}

export async function deleteReportEquipment(id: string): Promise<void> {
  await request<{ status: string }>(
    `${backendBaseUrl}/api/report/equipment/${encodeURIComponent(id)}`,
    { method: "DELETE" },
  );
}

// ---------------------------------------------------------------------------
// 年度报告配置（设计 §15.3、§12.1）
// ---------------------------------------------------------------------------

export interface PersonnelAssignment {
  personnel_id: string;
  full_name: string;
  organization: string | null;
  professional_title: string | null;
  role_code: string;
  sort_order: number;
  is_enabled: boolean;
  /** 停用的人还挂在配置上时为 true，生成前必须重新确认或替换。 */
  needs_reconfirmation: boolean;
}

export interface EquipmentAssignment {
  equipment_id: string;
  equipment_name: string;
  model_spec: string | null;
  purpose: string | null;
  sort_order: number;
  is_enabled: boolean;
  calibration_expired: boolean;
  needs_reconfirmation: boolean;
}

export interface ComparisonCandidate {
  inspection_year_id: string;
  inspection_year: number;
  status: string;
  report_number: string | null;
  overall_grade: string | null;
}

export interface InspectionReportSettings {
  inspection_year_id: string;
  inspection_year: number;
  template_id: string | null;
  template_name: string | null;
  template_is_usable: boolean;
  comparison_inspection_id: string | null;
  comparison_year: number | null;
  comparison_is_usable: boolean;
  personnel: PersonnelAssignment[];
  equipment: EquipmentAssignment[];
  configured_by_display_name: string | null;
  configured_at: string | null;
  /** 生成页要一眼看出还差什么，不用等点了生成才被预检打回来。 */
  blocking_notes: string[];
}

export interface InspectionReportSettingsInput {
  template_id: string | null;
  comparison_inspection_id: string | null;
  personnel: { personnel_id: string; role_code: string; sort_order: number }[];
  equipment: { equipment_id: string; purpose?: string; sort_order: number }[];
}

export async function fetchReportSettings(
  inspectionYearId: string,
): Promise<InspectionReportSettings> {
  return request<InspectionReportSettings>(
    `${backendBaseUrl}/api/inspection-years/${encodeURIComponent(inspectionYearId)}/report-settings`,
  );
}

export async function saveReportSettings(
  inspectionYearId: string,
  input: InspectionReportSettingsInput,
): Promise<InspectionReportSettings> {
  return request<InspectionReportSettings>(
    `${backendBaseUrl}/api/inspection-years/${encodeURIComponent(inspectionYearId)}/report-settings`,
    { method: "PUT", headers: JSON_HEADERS, body: JSON.stringify(input) },
  );
}

export async function fetchComparisonCandidates(
  inspectionYearId: string,
): Promise<ComparisonCandidate[]> {
  const body = await request<{ candidates: ComparisonCandidate[] }>(
    `${backendBaseUrl}/api/inspection-years/${encodeURIComponent(inspectionYearId)}` +
      "/report-settings/comparison-candidates",
  );
  return body.candidates;
}

// ---------------------------------------------------------------------------
// 生成前检查与生成任务（设计 §16、§17）
// ---------------------------------------------------------------------------

export interface PreflightFinding {
  code: string;
  message: string;
  severity: "blocking" | "warning";
}

export interface ReportPreflight {
  inspection_year_id: string;
  inspection_year: number;
  can_generate: boolean;
  findings: PreflightFinding[];
  summary: {
    defect_component_count: number;
    defect_observation_count: number;
    /** 去重后的来源病害数。与观测数的差就是构件范围拆分放大的部分（设计 §12.2）。 */
    source_defect_count: number;
    photo_count: number;
    overall_grade: string | null;
    structure_parts_with_data: string[];
    template_covered_parts: string[];
  };
}

export async function fetchReportPreflight(
  inspectionYearId: string,
): Promise<ReportPreflight> {
  return request<ReportPreflight>(
    `${backendBaseUrl}/api/inspection-years/${encodeURIComponent(inspectionYearId)}/report-preflight`,
  );
}

export type ReportJobStatus =
  | "queued"
  | "validating_data"
  | "assembling_docx"
  | "updating_fields"
  | "validating_docx"
  | "ready"
  | "failed"
  | "expired";

export interface ReportJobProgress {
  blocks_rendered?: string[];
  missing_placeholders?: string[];
  assemble_seconds?: number;
  updater?: string;
  page_count?: number | null;
  field_update_seconds?: number;
  field_update_queued_seconds?: number;
  section_count?: number;
  image_count?: number;
  /** 成品体积。一份大报告二十多兆，点下载之前得让人知道要下多大的东西。 */
  file_bytes?: number;
  blocking_findings?: PreflightFinding[];
}

export interface ReportJob {
  id: string;
  inspection_year_id: string;
  requested_by_user_id: string;
  status: ReportJobStatus;
  is_running: boolean;
  progress: ReportJobProgress;
  template_id: string | null;
  template_checksum: string | null;
  error_code: string | null;
  error_message: string | null;
  created_at: string;
  finished_at: string | null;
  expires_at: string | null;
  download_filename: string | null;
  can_download: boolean;
}

export async function createReportJob(inspectionYearId: string): Promise<ReportJob> {
  return request<ReportJob>(
    `${backendBaseUrl}/api/inspection-years/${encodeURIComponent(inspectionYearId)}/report-jobs`,
    { method: "POST", headers: JSON_HEADERS, body: "{}" },
  );
}

/**
 * 这个年度的「当前报告」，没生成过就是 null。
 *
 * 不取历史：系统不保存报告版本，临时文件到期即删，摆一张历史表只会让人以为那些旧
 * 文件还在。用户要的永远是「现在这份能不能下」。
 */
export async function fetchCurrentReportJob(
  inspectionYearId: string,
): Promise<ReportJob | null> {
  const body = await request<{ job: ReportJob | null }>(
    `${backendBaseUrl}/api/inspection-years/${encodeURIComponent(inspectionYearId)}` +
      "/report-jobs/current",
  );
  return body.job;
}

export async function fetchReportJob(jobId: string): Promise<ReportJob> {
  return request<ReportJob>(`${backendBaseUrl}/api/report-jobs/${encodeURIComponent(jobId)}`);
}

/** 下载生成好的报告。同样走 fetch：导航不带 Authorization 头，会 401。 */
export async function downloadReportJob(
  jobId: string,
  fallbackName: string,
): Promise<void> {
  await downloadFile(
    `${backendBaseUrl}/api/report-jobs/${encodeURIComponent(jobId)}/download`,
    fallbackName,
  );
}

// ---------------------------------------------------------------------------
// 展示用的小工具
// ---------------------------------------------------------------------------

/** 阶段名。与后端状态字面值一一对应，前端不自己再编一套（设计 §17.2）。 */
export const REPORT_JOB_STAGE_LABELS: Record<ReportJobStatus, string> = {
  queued: "排队中",
  validating_data: "校验数据",
  assembling_docx: "装配文档",
  updating_fields: "更新目录与页码",
  validating_docx: "校验成品",
  ready: "可下载",
  failed: "失败",
  expired: "已过期",
};

/** 人员角色代码 -> 报告里的写法。代码是稳定标识，中文只在这里出现一次。 */
export const PERSONNEL_ROLE_LABELS: Record<string, string> = {
  approver: "批准",
  reviewer: "审核",
  lead_inspector: "检测负责人",
  compiler: "编制",
  participant: "参加人员",
};

export const STRUCTURE_PART_LABELS: Record<string, string> = {
  SUPERSTRUCTURE: "上部结构",
  SUBSTRUCTURE: "下部结构",
  DECK: "桥面系",
  WHOLE_BRIDGE: "全桥",
  OTHER: "其他",
};

export function personnelRoleLabel(code: string): string {
  return PERSONNEL_ROLE_LABELS[code] ?? code;
}

export function structurePartLabel(code: string): string {
  return STRUCTURE_PART_LABELS[code] ?? code;
}
