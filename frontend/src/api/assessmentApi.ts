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

export interface AssessmentPreviewResponse {
  client_revision: number;
  input_checksum: string;
  input_summary: Record<string, unknown>;
  standard: AssessmentStandardIdentity;
  result: AssessmentResult | null;
  issues: AssessmentIssue[];
  assessment_run_id: string | null;
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
