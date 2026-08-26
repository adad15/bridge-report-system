import type { BridgeAnnualInspectionData } from "../contracts/annualInspection";
import { request } from "./apiClient";

export interface AssessmentStandardIdentity {
  standard_id: string;
  standard_code: string;
  standard_name: string;
  official_edition: string;
  package_version: string;
  content_checksum: string;
  algorithm_id: string;
}

export interface AssessmentIssue {
  code: string;
  message: string;
  entity_type: string;
  entity_id: string;
  field_path: string;
  rule_id: string;
}

export interface AssessmentTrace {
  step: string;
  rule_id: string;
  entity_id: string;
  source_reference: string;
  inputs: Record<string, unknown>;
  output: Record<string, unknown>;
}

export interface AssessmentCategoryResult {
  component_type_id: string;
  component_type_name?: string;
  structure_part: string;
  major: boolean;
  score: number;
  grade: number;
  mean_component_score: number;
  minimum_component_score: number;
  configured_weight: number;
  effective_weight: number;
  low_score_passthrough: boolean;
  component_count_factor: number | null;
  components: Array<{
    component_instance_id: string;
    component_type_id: string;
    structure_part: string;
    major: boolean;
    score: number;
    ordered_deductions: number[];
    defects: Array<{
      defect_indicator_id: string;
      scale: number;
      deduction: number;
      deduction_rule_id: string;
      source_reference: string;
    }>;
  }>;
}

export interface AssessmentResult {
  standard_id: string;
  package_version: string;
  bridge_type_id: string;
  overall_score: number;
  calculated_grade: number;
  final_grade: number;
  explanation: string;
  structure_parts: Array<{
    structure_part: string;
    score: number;
    grade: number;
    overall_weight: number;
    categories: AssessmentCategoryResult[];
  }>;
  triggered_controls: Array<{
    control_id: string;
    source_reference: string;
    label: string;
    result_grade: number | null;
  }>;
  trace: AssessmentTrace[];
}

/**
 * 试算与已入库回执共有的三件套。评定区只消费这三个字段，两条来路因此能共用同一套渲染，
 * 也就不会出现"入库前看到的分数结构和入库后看到的不是一回事"。
 */
export interface AssessmentReport {
  standard: AssessmentStandardIdentity;
  result: AssessmentResult | null;
  issues: AssessmentIssue[];
}

export interface AssessmentPreviewResponse extends AssessmentReport {
  client_revision: number;
  input_checksum: string;
  input_summary: Record<string, unknown>;
  assessment_run_id: string | null;
}

/** 入库时写下的那一次正式评定。分数不重算——规则包升级后重算会和报告里的数字对不上。 */
export interface ConfirmedAssessmentResponse extends AssessmentReport {
  assessment_run_id: string;
  formal_revision_number: number;
  /** false 表示该年度后来又被修订过，这份是历史版本。 */
  is_current: boolean;
  confirmed_at: string | null;
  inspection_year: number;
  inspection_year_version: number;
  /** 年度行本身是否仍是当前有效版本；false 表示这一年后来被修订过。 */
  inspection_year_is_current: boolean;
}

export function previewAssessment(
  baseUrl: string,
  importRecordId: string,
  draft: BridgeAnnualInspectionData,
  clientRevision: number,
  editLockToken: string,
  signal?: AbortSignal,
): Promise<AssessmentPreviewResponse> {
  return request<AssessmentPreviewResponse>(
    `${baseUrl}/api/import-records/${encodeURIComponent(importRecordId)}/assessment-preview`,
    {
      method: "POST",
      headers: { "Content-Type": "application/json", "X-Edit-Lock-Token": editLockToken },
      body: JSON.stringify({ draft, client_revision: clientRevision }),
      signal,
    },
  );
}

/** 读取某条导入记录入库时写下的正式评定；没有入库过时后端返回 assessment_report_not_found。 */
export function fetchConfirmedAssessment(
  baseUrl: string,
  importRecordId: string,
  signal?: AbortSignal,
): Promise<ConfirmedAssessmentResponse> {
  return request<ConfirmedAssessmentResponse>(
    `${baseUrl}/api/import-records/${encodeURIComponent(importRecordId)}/assessment-report`,
    { signal },
  );
}
