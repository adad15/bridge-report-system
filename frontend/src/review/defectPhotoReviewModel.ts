import type { AssessmentIssue } from "../api/assessmentApi";
import type { DefectMatchCandidate, DefectMatchResult } from "../api/defectMatchingApi";
import type { RatingTreeNode } from "../api/ratingTreeApi";
import { buildDefectPhotoCards, type DefectPhotoCard } from "./defectPhotoCards";
import type {
  BridgeAnnualInspectionData,
  DefectCandidate,
  PhotoCandidate,
} from "../contracts/annualInspection";

export type DefectReviewProblemCategory = "component" | "defect_type" | "scale" | "photo" | "other";
export type DefectReviewFilter = "all" | "batchable" | "needs_attention" | "confirmed";

/**
 * 工具栏的问题筛选。与匹配结果类型直接对齐，用户不用先猜"病害类型"这类
 * 分类名到底覆盖了组合病害还是多候选。
 */
export type DefectReviewIssueFilter =
  | "all"
  | "composite"
  | "candidates"
  | "unmatched"
  | "component_unbound"
  | "scale_pending"
  | "photo_pending";

/** 左侧列表一眼可读的匹配状态。颜色只做辅助，状态文字始终可见。 */
export type DefectMatchState =
  | "ignored"
  | "confirmed"
  | "manual"
  | "auto_bound"
  | "composite"
  | "candidates"
  | "unmatched"
  | "prerequisite_missing"
  | "service_error"
  | "pending";

export interface DefectReviewProblem {
  code: string;
  category: DefectReviewProblemCategory;
  message: string;
}

export interface DefectReviewRow {
  candidateId: string;
  defect: DefectCandidate;
  photos: PhotoCandidate[];
  problems: DefectReviewProblem[];
  /** 当前详情可人工确认；范围拆分提示需要靠这次确认消除，因此不作为单条确认阻断项。 */
  confirmEligible: boolean;
  /** 无任何待核对问题，允许进入工具栏批量确认。 */
  batchEligible: boolean;
  status: "batchable" | "needs_attention" | "confirmed" | "ignored";
  matchState: DefectMatchState;
  matchLabel: string;
  matchResult: DefectMatchResult | null;
  matchCandidates: DefectMatchCandidate[];
  /** 与照片面板渲染的是同一份卡片，问题和界面不会各算一次。 */
  photoCards: DefectPhotoCard[];
  ratingTreeNode: RatingTreeNode | null;
}

export interface DefectPhotoReviewSummary {
  all: number;
  /** 待处理 = 既不能批量确认也还没确认的记录。 */
  pending: number;
  batchable: number;
  confirmed: number;
  composite: number;
  candidates: number;
  unmatched: number;
}

export interface DefectPhotoReviewModel {
  rows: DefectReviewRow[];
  summary: DefectPhotoReviewSummary;
  safeCandidateIds: Set<string>;
}

export interface DefectPhotoReviewModelInput {
  draft: BridgeAnnualInspectionData;
  /** null 表示当前年度尚未锁定评定树。 */
  ratingTreeVersionId: string | null;
  ratingTreeNodes?: RatingTreeNode[];
  applicableTreeNodeIdsByComponent?: ReadonlyMap<string, ReadonlySet<string>>;
  treeRulesReady?: boolean;
  assessmentIssues: AssessmentIssue[];
  /** 后端批量匹配的临时结果，按 candidate_id 索引；不进入正式病害档案。 */
  matchResults?: ReadonlyMap<string, DefectMatchResult>;
  filter?: DefectReviewFilter;
  issueFilter?: DefectReviewIssueFilter | null;
  search?: string;
}

/**
 * 后端试算与前端派生会把同一个毛病各说一遍。addProblem 只按 code 去重，两边 code
 * 不同就都留下了，详情面板于是上下两行同义提示，列表那两个问题槽位也被占满。
 * 这里按语义对齐：后端问题若已有等价的前端问题，就不再重复列出（前端措辞更短）。
 *
 * 键必须与 AssessmentService.cpp 实际发出的 code 逐字一致——评定树那几条的前缀是
 * assessment_rating_tree_（不是 assessment_defect_rating_tree_），写错就静默失效。
 */
const ASSESSMENT_PROBLEM_ALIASES: Record<string, string> = {
  assessment_defect_component_unmatched: "component_required",
  assessment_defect_scale_required: "scale_not_allowed",
  assessment_defect_scale_not_allowed: "scale_not_allowed",
  assessment_rating_tree_node_required: "rating_tree_node_required",
  assessment_rating_tree_node_not_applicable: "rating_tree_node_not_applicable",
  assessment_rating_tree_scale_not_allowed: "scale_not_allowed",
};

const MATCH_METHOD_LABELS: Record<string, string> = {
  exact: "规范名称精确匹配",
  controlled_alias: "受控别名",
  controlled_keyword: "受控关键词",
  fuzzy_candidate: "文字相似（仅候选）",
  source_indicator: "来源软件标注",
  manual: "人工选择",
};

export function matchMethodLabel(method: string | null | undefined): string {
  if (!method) return "尚未匹配";
  return MATCH_METHOD_LABELS[method] ?? method;
}

// 尺寸原文看着像有数值/单位、却一条结构化尺寸都没解析出来。导入器只在解析那一刻
// 打警告，用户后来在详情里改了尺寸原文并不会重打（edit_measurement_text 只重解析），
// 所以这里补一条兜底判定。与 measurements.py 的 should_warn 用同一个粗粒度模式。
const MEASUREMENT_HINT_PATTERN = /\d+(\.\d+)?\s*(m|mm|cm|m2|m²|处|条)/;

function addProblem(
  problems: DefectReviewProblem[],
  code: string,
  category: DefectReviewProblemCategory,
  message: string,
): void {
  if (!problems.some((problem) => problem.code === code)) {
    problems.push({ code, category, message });
  }
}

/**
 * 匹配状态由"草稿里已经有什么"与"后端这次算出什么"共同决定：
 * 已确认与人工选择永远优先，自动结果绝不改写它们的显示。
 */
function deriveMatchState(
  defect: DefectCandidate,
  match: DefectMatchResult | null,
  ratingTreeNode: RatingTreeNode | null,
): { state: DefectMatchState; label: string } {
  const nodeName = ratingTreeNode?.display_name ?? defect.defect_type;
  if (defect.group_review_status === "已确认") {
    return { state: "confirmed", label: nodeName ? `已确认：${nodeName}` : "已确认" };
  }
  if (defect.rating_tree_match_method === "manual" && defect.rating_tree_node_id) {
    return { state: "manual", label: nodeName ? `人工选择：${nodeName}` : "人工选择" };
  }
  if (
    defect.rating_tree_node_id &&
    defect.rating_tree_match_method &&
    defect.rating_tree_match_method !== "fuzzy_candidate"
  ) {
    return { state: "auto_bound", label: nodeName ? `自动匹配：${nodeName}` : "自动匹配" };
  }
  if (!match) return { state: "pending", label: "待匹配" };
  switch (match.outcome) {
    case "composite":
      return { state: "composite", label: "疑似组合病害" };
    case "candidates":
      return { state: "candidates", label: `候选 ${match.candidates.length} 项` };
    case "prerequisite_missing":
      return { state: "prerequisite_missing", label: "依赖缺失" };
    case "service_error":
      return { state: "service_error", label: "匹配服务异常" };
    case "auto_bound":
      return { state: "auto_bound", label: "自动匹配待写入" };
    default:
      return { state: "unmatched", label: "未找到匹配" };
  }
}

function analyzeDefect(
  draft: BridgeAnnualInspectionData,
  defect: DefectCandidate,
  assessmentIssues: AssessmentIssue[],
  repeatedPhotoNumbers: Set<string>,
  ratingTreeVersionId: string | null,
  ratingTreeNodes: ReadonlyMap<string, RatingTreeNode>,
  applicableTreeNodeIdsByComponent: ReadonlyMap<string, ReadonlySet<string>>,
  treeRulesReady: boolean,
  match: DefectMatchResult | null,
): DefectReviewRow {
  const problems: DefectReviewProblem[] = [];
  const photoCards = buildDefectPhotoCards(draft, defect);
  const photos = photoCards
    .map((card) => card.photo)
    .filter((photo): photo is PhotoCandidate => photo !== null);
  const ratingTreeNode = defect.rating_tree_node_id
    ? ratingTreeNodes.get(defect.rating_tree_node_id) ?? null
    : null;

  if (!defect.bridge_component_id || !defect.standard_component_category_id) {
    addProblem(problems, "component_required", "component", "尚未选择实际构件。");
  }
  if (!ratingTreeVersionId) {
    addProblem(problems, "rating_tree_required", "defect_type", "当前年度尚未锁定评定树。");
  } else if (!defect.rating_tree_node_id) {
    addProblem(problems, "rating_tree_node_required", "defect_type", "尚未选择评定树病害。");
  } else {
    if (defect.rating_tree_version_id !== ratingTreeVersionId) {
      addProblem(problems, "rating_tree_version_mismatch", "defect_type", "病害关联的评定树版本与当前年度不一致。");
    }
    if (!treeRulesReady) {
      addProblem(problems, "rating_tree_loading", "other", "正在加载评定树规则。");
    } else if (!ratingTreeNode) {
      addProblem(problems, "rating_tree_node_unknown", "defect_type", "评定树病害节点已不存在。");
    } else if (
      !defect.bridge_component_id ||
      !applicableTreeNodeIdsByComponent.get(defect.bridge_component_id)?.has(ratingTreeNode.id)
    ) {
      addProblem(problems, "rating_tree_node_not_applicable", "defect_type", "评定树病害不适用于当前实际构件。");
    }
  }
  if (defect.rating_tree_match_method === "fuzzy_candidate") {
    addProblem(problems, "rating_tree_fuzzy_review_required", "defect_type", "模糊匹配建议需要人工确认。");
  }
  if (
    ratingTreeNode?.is_scoring &&
    (defect.defect_scale === null ||
      defect.defect_scale === undefined ||
      !ratingTreeNode.allowed_scales.includes(defect.defect_scale))
  ) {
    addProblem(problems, "scale_not_allowed", "scale", "病害标度不在评定树允许范围内。");
  }

  // 匹配服务给出的问题：依赖缺失、服务故障和真的没规则必须能分开看。
  if (match && !match.skipped && !defect.rating_tree_node_id) {
    if (match.outcome === "composite") {
      addProblem(problems, "rating_tree_composite_defect", "defect_type", "疑似组合病害，请拆分或确认为单一病害。");
    } else if (match.outcome === "candidates") {
      addProblem(
        problems,
        "rating_tree_multiple_candidates",
        "defect_type",
        `匹配到 ${match.candidates.length} 个候选，请人工选择。`,
      );
    } else if (match.outcome === "prerequisite_missing") {
      addProblem(problems, "rating_tree_prerequisite_missing", "component", match.reason_message ?? "匹配依赖尚未补齐。");
    } else if (match.outcome === "service_error") {
      addProblem(problems, "rating_tree_matcher_failed", "other", match.reason_message ?? "评定树匹配服务失败，请重试。");
    }
  }

  // 照片关系随病害组一起确认；这里只检查引用结论和归档文件是否完整。
  for (const card of photoCards) {
    if (repeatedPhotoNumbers.has(card.photoNumber)) {
      addProblem(problems, "photo_number_conflict", "photo", `照片编号 ${card.photoNumber} 被多条病害引用。`);
    }
    if (card.kind === "missing") {
      if (!card.acknowledgedMissing) {
        addProblem(problems, "photo_reference_pending", "photo", `照片编号 ${card.photoNumber} 尚未核对。`);
      }
      continue;
    }
    const photo = card.photo!;
    const archived = Boolean(photo.extracted_file.archive_relative_path);
    if (!archived) {
      addProblem(problems, "photo_archive_missing", "photo", `照片 ${card.photoNumber} 的归档文件缺失。`);
    }
  }

  if (
    defect.measurement_text &&
    MEASUREMENT_HINT_PATTERN.test(defect.measurement_text) &&
    defect.measurements.length === 0
  ) {
    addProblem(problems, "measurement_parse_low_confidence", "other", "尺寸表达存在数值线索但未能结构化，请人工确认。");
  }

  for (const warning of defect.warnings) {
    addProblem(problems, warning.code, "other", warning.message);
  }
  for (const issue of assessmentIssues) {
    if (issue.entity_type === "defect" && issue.entity_id === defect.candidate_id) {
      const alias = ASSESSMENT_PROBLEM_ALIASES[issue.code];
      if (alias && problems.some((problem) => problem.code === alias)) continue;
      const category: DefectReviewProblemCategory =
        issue.field_path === "defect_scale"
          ? "scale"
          : issue.field_path === "defect_type" ||
              issue.field_path === "standard_defect_indicator_id"
            ? "defect_type"
            : issue.field_path.startsWith("photo")
              ? "photo"
              : "other";
      addProblem(problems, issue.code, category, issue.message);
    }
  }
  for (const item of [...draft.warnings, ...draft.errors]) {
    if (item.target_candidate_id === defect.candidate_id) {
      addProblem(problems, item.code, "other", item.message);
    }
  }

  const ignored = defect.review_status === "已忽略";
  const confirmed = !ignored && defect.group_review_status === "已确认" && problems.length === 0;
  const hasRangeSplitReviewWarning = defect.warnings.some(
    (warning) => warning.code === "component_range_split_review_required",
  );
  const hasIndividualConfirmationBlocker = problems.some(
    (problem) =>
      problem.code !== "component_range_split_review_required" ||
      !hasRangeSplitReviewWarning,
  );
  const confirmEligible =
    !ignored &&
    defect.group_review_status !== "已确认" &&
    !hasIndividualConfirmationBlocker;
  const batchEligible = !ignored && !confirmed && problems.length === 0;
  const derived = ignored
    ? { state: "ignored" as const, label: "已忽略" }
    : deriveMatchState(defect, match, ratingTreeNode);
  return {
    candidateId: defect.candidate_id,
    defect,
    photos,
    problems,
    confirmEligible,
    batchEligible,
    status: ignored
      ? "ignored"
      : confirmed
        ? "confirmed"
        : batchEligible
          ? "batchable"
          : "needs_attention",
    matchState: derived.state,
    matchLabel: derived.label,
    matchResult: match,
    matchCandidates: match && !defect.rating_tree_node_id ? match.candidates : [],
    photoCards,
    ratingTreeNode,
  };
}

// 默认优先级：先把必须人工判断的推到最前，可批量确认和已确认沉底。
const SORT_RANK: Record<DefectMatchState, number> = {
  composite: 0,
  ignored: 7,
  candidates: 1,
  unmatched: 2,
  service_error: 2,
  prerequisite_missing: 3,
  pending: 3,
  manual: 3,
  auto_bound: 3,
  confirmed: 5,
};

function sortRank(row: DefectReviewRow): number {
  if (row.status === "ignored") return 7;
  if (row.status === "confirmed") return 6;
  if (row.status === "batchable") return 5;
  return SORT_RANK[row.matchState];
}

function matchesIssueFilter(
  row: DefectReviewRow,
  issueFilter: DefectReviewIssueFilter,
): boolean {
  switch (issueFilter) {
    case "all":
      return true;
    case "composite":
      return row.matchState === "composite";
    case "candidates":
      return row.matchState === "candidates";
    case "unmatched":
      return row.matchState === "unmatched";
    case "component_unbound":
      return row.problems.some((problem) => problem.category === "component");
    case "scale_pending":
      return row.problems.some((problem) => problem.category === "scale");
    case "photo_pending":
      return row.problems.some((problem) => problem.category === "photo");
    default:
      return true;
  }
}

export function buildDefectPhotoReviewModel(
  input: DefectPhotoReviewModelInput,
): DefectPhotoReviewModel {
  const ratingTreeNodes = new Map(
    (input.ratingTreeNodes ?? []).map((node) => [node.id, node] as const),
  );
  const photoNumberCounts = new Map<string, number>();
  for (const defect of input.draft.defects) {
    for (const reference of defect.photo_references) {
      photoNumberCounts.set(
        reference.photo_number,
        (photoNumberCounts.get(reference.photo_number) ?? 0) + 1,
      );
    }
  }
  const repeatedPhotoNumbers = new Set(
    [...photoNumberCounts.entries()]
      .filter(([, count]) => count > 1)
      .map(([number]) => number),
  );
  const allRows = input.draft.defects.map((defect) =>
    analyzeDefect(
      input.draft,
      defect,
      input.assessmentIssues,
      repeatedPhotoNumbers,
      input.ratingTreeVersionId,
      ratingTreeNodes,
      input.applicableTreeNodeIdsByComponent ?? new Map(),
      input.treeRulesReady ?? false,
      input.matchResults?.get(defect.candidate_id) ?? null,
    ),
  );
  const summary = {
    all: allRows.length,
    pending: allRows.filter((row) => row.status === "needs_attention").length,
    batchable: allRows.filter((row) => row.status === "batchable").length,
    confirmed: allRows.filter((row) => row.status === "confirmed").length,
    composite: allRows.filter((row) => row.matchState === "composite").length,
    candidates: allRows.filter((row) => row.matchState === "candidates").length,
    unmatched: allRows.filter((row) => row.matchState === "unmatched").length,
  };
  const search = input.search?.trim().toLocaleLowerCase() ?? "";
  const ordered = allRows
    .map((row, index) => ({ row, index }))
    .sort((left, right) => {
      const rank = sortRank(left.row) - sortRank(right.row);
      return rank !== 0 ? rank : left.index - right.index;
    })
    .map((item) => item.row);
  const rows = ordered.filter((row) => {
    if (input.filter && input.filter !== "all") {
      if (input.filter === "batchable" && row.status !== "batchable") return false;
      if (input.filter === "needs_attention" && row.status !== "needs_attention") return false;
      if (input.filter === "confirmed" && row.status !== "confirmed") return false;
    }
    if (
      input.issueFilter &&
      input.issueFilter !== "all" &&
      !matchesIssueFilter(row, input.issueFilter)
    ) {
      return false;
    }
    if (!search) return true;
    const haystack = [
      row.defect.component_number,
      row.defect.defect_location,
      row.defect.defect_type,
      row.matchLabel,
      ...row.defect.photo_references.map((reference) => reference.photo_number),
    ].join(" ").toLocaleLowerCase();
    return haystack.includes(search);
  });
  return {
    rows,
    summary,
    safeCandidateIds: new Set(
      allRows.filter((row) => row.batchEligible).map((row) => row.candidateId),
    ),
  };
}
