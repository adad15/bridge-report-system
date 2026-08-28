import type { DefectCandidate } from "../contracts/annualInspection";
import type { RatingTreeMatchMethod } from "../contracts/resolution";
import {
  EMPTY_RESOLUTION_INDEX,
  resolutionOf,
  type ResolutionIndex,
} from "../review/resolutionIndex";
import { ApiError, request } from "./apiClient";

/** 与后端 RatingTreeMatchOutcome 一一对应的统一结果类型。 */
export type DefectMatchOutcome =
  | "auto_bound"
  | "candidates"
  | "composite"
  | "unmatched"
  | "prerequisite_missing"
  | "service_error";

export interface DefectMatchCandidate {
  rating_tree_node_id: string;
  display_name: string;
  match_method: RatingTreeMatchMethod;
  evidence: string;
}

export interface DefectMatchResult {
  candidate_id: string;
  outcome: DefectMatchOutcome;
  /** 人工选择、已确认或已忽略：自动结果不参与，也不覆盖现状。 */
  skipped: boolean;
  rating_tree_node_id: string | null;
  match_method: RatingTreeMatchMethod | null;
  match_evidence: string | null;
  reason_code: string | null;
  reason_message: string | null;
  candidates: DefectMatchCandidate[];
}

export interface DefectMatchSummary {
  processed: number;
  auto_bound: number;
  candidates: number;
  composite: number;
  unmatched: number;
  prerequisite_missing: number;
  failed: number;
  skipped: number;
}

export interface DefectMatchReport {
  summary: DefectMatchSummary;
  results: DefectMatchResult[];
  rating_tree_version_id: string;
}

/**
 * 匹配请求只带匹配真正用到的字段，几百条病害也只发一个请求。
 *
 * 绑定构件与已有节点 5.0 起不在草稿里，由调用方从工作区读模型取好传进来。
 */
function matchInput(
  defect: DefectCandidate,
  resolution: {
    bridgeComponentId: string | null;
    ratingTreeNodeId: string | null;
    ratingMatchMethod: RatingTreeMatchMethod | null;
  },
) {
  return {
    candidate_id: defect.candidate_id,
    bridge_component_id: resolution.bridgeComponentId,
    defect_type: defect.defect_type,
    defect_description: defect.defect_description,
    defect_location: defect.defect_location,
    source_defect_group_id: defect.source_defect_group_id ?? null,
    source_defect_group_number: defect.source_defect_group_number ?? null,
    source_defect_indicator_id: defect.source_defect_indicator_id ?? null,
    source_defect_indicator_number: defect.source_defect_indicator_number ?? null,
    rating_tree_node_id: resolution.ratingTreeNodeId,
    rating_tree_match_method: resolution.ratingMatchMethod,
    review_status: defect.review_status,
    group_review_status: defect.group_review_status,
  };
}

/**
 * 批量匹配当前草稿里的病害。后端只做只读计算：候选与原因码是临时结果，
 * 自动绑定由页面写进本地草稿，保存时服务端按同一份规则复核。
 */
export function matchDefectRatingTreeNodes(
  baseUrl: string,
  importRecordId: string,
  defects: DefectCandidate[],
  resolution: ResolutionIndex = EMPTY_RESOLUTION_INDEX,
  candidateIds?: string[],
): Promise<DefectMatchReport> {
  return request<DefectMatchReport>(
    `${baseUrl}/api/import-records/${encodeURIComponent(importRecordId)}/defect-rating-tree-matches`,
    {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        defects: defects.map((defect) =>
          matchInput(defect, resolutionOf(resolution, defect.candidate_id))),
        ...(candidateIds ? { candidate_ids: candidateIds } : {}),
      }),
    },
  );
}

const MATCH_ERROR_MESSAGES: Record<string, string> = {
  rating_tree_not_bound: "本年度尚未锁定评定树，无法匹配评定树病害。",
  rating_tree_catalog_unavailable:
    "评定树目录当前不可用，匹配结果不代表“无匹配”，请重试。",
};

/** 评定树装载失败必须显式报错，不能被当成"这批病害都没有匹配结果"。 */
export function defectMatchErrorMessage(error: unknown): string {
  if (error instanceof ApiError) {
    return MATCH_ERROR_MESSAGES[error.code] ?? error.message;
  }
  return "评定树匹配服务暂时不可用，请稍后重试。";
}
