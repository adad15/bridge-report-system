import type { AssessmentIssue } from "../api/assessmentApi";
import {
  EMPTY_RESOLUTION_INDEX,
  resolutionOf,
  type DefectResolution,
  type ResolutionIndex,
} from "./resolutionIndex";
import type { DefectMatchCandidate, DefectMatchResult } from "../api/defectMatchingApi";
import type { RatingTreeNode, RatingTreeNodeSummary } from "../api/ratingTreeApi";
import { buildDefectPhotoCards, type DefectPhotoCard } from "./defectPhotoCards";
import { isHumanAcknowledgeableWarning } from "./defectWarnings";
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
  /**
   * 该来源病害的解析状态（构件绑定、评分树节点）。
   *
   * 5.0 起这些不在 `defect` 上，挂到行上是为了让每个消费方读 `row.resolution` 就够，
   * 不必各自再把索引传一遍——传的地方越多，漏传一处就越可能出现"这一处以为没绑定"。
   */
  resolution: DefectResolution;
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
  /** 各部件的条数，按走查顺序。未绑定构件的归在 UNBOUND_PART_FILTER 名下。 */
  parts: Array<{ name: string; count: number }>;
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
  /**
   * 按来源病害索引的解析状态（构件绑定、评分树节点）。
   *
   * 5.0 起这些不在草稿里，由 `GET /resolution-workspace` 提供。没给时一律按"未解析"
   * 处理：那正是工作区还没加载回来的真实状态，编一个"已绑定"出来只会让界面先绿后红。
   */
  resolution?: ResolutionIndex;
  /** null 表示当前年度尚未锁定评定树。 */
  ratingTreeVersionId: string | null;
  ratingTreeNodes?: RatingTreeNode[];
  /** 适用节点接口已经能证明节点存在、适用范围及是否计分，无需等待详情接口。 */
  ratingTreeNodeSummaries?: RatingTreeNodeSummary[];
  applicableTreeNodeIdsByComponent?: ReadonlyMap<string, ReadonlySet<string>>;
  treeRulesReady?: boolean;
  /**
   * 构件 id → 走查次序（后端排好后的下标）。给了就按部件顺序摆行：
   * 板 → 铰缝 → 支座 → 墩柱 → 盖梁 → …，同一部件内部再按现有的优先级排。
   * 没给（还没取到）时退回纯优先级排序，不至于把列表打乱。
   */
  componentOrder?: Map<string, number>;
  /** 构件 id → 部件名（"板""铰缝"…）。用于按部件筛选与统计每个部件多少条。 */
  componentPart?: Map<string, string>;
  /** 只看某个部件；UNBOUND_PART_FILTER 表示只看还没绑定构件的。 */
  partFilter?: string | null;
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
  resolution: DefectResolution,
  match: DefectMatchResult | null,
  ratingTreeNode: RatingTreeNodeSummary | null,
): { state: DefectMatchState; label: string } {
  const nodeName = ratingTreeNode?.display_name ?? defect.defect_type;
  if (defect.group_review_status === "已确认") {
    return { state: "confirmed", label: nodeName ? `已确认：${nodeName}` : "已确认" };
  }
  if (resolution.ratingMatchMethod === "manual" && resolution.ratingTreeNodeId) {
    return { state: "manual", label: nodeName ? `人工选择：${nodeName}` : "人工选择" };
  }
  if (
    resolution.ratingTreeNodeId &&
    resolution.ratingMatchMethod &&
    resolution.ratingMatchMethod !== "fuzzy_candidate"
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
  resolution: DefectResolution,
  assessmentIssues: AssessmentIssue[],
  repeatedPhotoNumbers: Set<string>,
  ratingTreeVersionId: string | null,
  ratingTreeNodes: ReadonlyMap<string, RatingTreeNode>,
  ratingTreeNodeSummaries: ReadonlyMap<string, RatingTreeNodeSummary>,
  applicableTreeNodeIdsByComponent: ReadonlyMap<string, ReadonlySet<string>>,
  treeRulesReady: boolean,
  match: DefectMatchResult | null,
): DefectReviewRow {
  const problems: DefectReviewProblem[] = [];
  const photoCards = buildDefectPhotoCards(draft, defect);
  const photos = photoCards
    .map((card) => card.photo)
    .filter((photo): photo is PhotoCandidate => photo !== null);
  const ratingTreeNode = resolution.ratingTreeNodeId
    ? ratingTreeNodes.get(resolution.ratingTreeNodeId) ?? null
    : null;
  const ratingTreeNodeSummary = resolution.ratingTreeNodeId
    ? ratingTreeNode ?? ratingTreeNodeSummaries.get(resolution.ratingTreeNodeId) ?? null
    : null;

  // 判据是"有没有活动实例绑上构件"，不是"能不能归结到单一构件"：区间展开的病害
  // 一条挂三件构件，bridgeComponentId 按约定为 null，用它判会把这条当成从没绑过。
  //
  // componentIds 为空时回落到单一 id：单构件是最常见的情况，两种构造方式都该判一致。
  const boundComponentIds = resolution.componentIds.length > 0
    ? resolution.componentIds
    : resolution.bridgeComponentId ? [resolution.bridgeComponentId] : [];

  if (boundComponentIds.length === 0) {
    addProblem(problems, "component_required", "component", "尚未选择实际构件。");
  }
  if (!ratingTreeVersionId) {
    addProblem(problems, "rating_tree_required", "defect_type", "当前年度尚未锁定评定树。");
  } else if (!resolution.ratingTreeNodeId) {
    addProblem(problems, "rating_tree_node_required", "defect_type", "尚未选择评定树病害。");
  } else {
    if (resolution.ratingTreeVersionId !== ratingTreeVersionId) {
      addProblem(problems, "rating_tree_version_mismatch", "defect_type", "病害关联的评定树版本与当前年度不一致。");
    }
    if (!treeRulesReady) {
      addProblem(problems, "rating_tree_loading", "other", "正在加载评定树规则。");
    } else if (!ratingTreeNodeSummary) {
      addProblem(problems, "rating_tree_node_unknown", "defect_type", "评定树病害节点已不存在。");
    } else if (
      // 展开到多件构件时，节点必须对**每一件**都适用：只要有一件不适用，那一条实例
      // 就会在确认阶段被拒，届时症状离这里已经很远。
      boundComponentIds.length === 0 ||
      !boundComponentIds.every((componentId) =>
        applicableTreeNodeIdsByComponent.get(componentId)?.has(ratingTreeNodeSummary.id))
    ) {
      addProblem(problems, "rating_tree_node_not_applicable", "defect_type", "评定树病害不适用于当前实际构件。");
    }
  }
  if (resolution.ratingMatchMethod === "fuzzy_candidate") {
    addProblem(problems, "rating_tree_fuzzy_review_required", "defect_type", "模糊匹配建议需要人工确认。");
  }
  if (ratingTreeNodeSummary?.is_scoring) {
    const allowedScales = ratingTreeNode?.allowed_scales ?? ratingTreeNodeSummary.allowed_scales;
    if (!allowedScales) {
      addProblem(problems, "rating_tree_node_loading", "scale", "正在加载病害标度规则。");
    } else if (
      defect.defect_scale === null ||
      defect.defect_scale === undefined ||
      !allowedScales.includes(defect.defect_scale)
    ) {
      addProblem(problems, "scale_not_allowed", "scale", "病害标度不在评定树允许范围内。");
    }
  }

  // 匹配服务给出的问题：依赖缺失、服务故障和真的没规则必须能分开看。
  if (match && !match.skipped && !resolution.ratingTreeNodeId) {
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
  // "请人工确认"类的警告不该算进阻断：确认本身就是对它的答复，拿它挡确认就成了死循环
  // ——警告只有确认才消得掉，确认又被警告拦着。名单见 defectWarnings.ts。
  //
  // 确认前后用的是同一份口径：确认前决定按钮能不能点，确认后决定这条算不算办完。
  // 两处若不一致，就会出现"点得动、点完还挂着待处理"的怪状态。
  const blockingProblems = problems.filter(
    (problem) => !isHumanAcknowledgeableWarning(problem.code),
  );
  const confirmed =
    !ignored && defect.group_review_status === "已确认" && blockingProblems.length === 0;
  const confirmEligible =
    !ignored && defect.group_review_status !== "已确认" && blockingProblems.length === 0;
  // 批量确认没有"人看一眼"这一步，正是这些警告要求的东西，所以它仍按最严的口径走：
  // 一条问题都不许剩。要了结这类警告只能逐条进详情确认。
  const batchEligible = !ignored && !confirmed && problems.length === 0;
  const derived = ignored
    ? { state: "ignored" as const, label: "已忽略" }
    : deriveMatchState(defect, resolution, match, ratingTreeNodeSummary);
  return {
    candidateId: defect.candidate_id,
    defect,
    resolution,
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
    matchCandidates: match && !resolution.ratingTreeNodeId ? match.candidates : [],
    photoCards,
    ratingTreeNode,
  };
}

// 还没绑定构件的病害不属于任何部件，但同样要能单独筛出来。
export const UNBOUND_PART_FILTER = "__unbound__";

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

/**
 * 每个照片编号被几条病害占着。
 *
 * "占着"是指报告里最终会印出一张这个编号的照片，或 Word 还在要这张图：
 *   ① 挂在这条病害上的照片，编号一定会写进报告；
 *   ② photo_references 里还没确认缺图的编号，Word 仍承诺着一张。
 * 两者先按病害去重再跨病害相加——同一条病害既引用 2.1-1 又挂着 2.1-1 是同一份占用，
 * 不该自己跟自己冲突。
 *
 * 不算的两种：已忽略的病害不入库，占不住任何编号；已确认缺图的引用等于当面认了
 * "原报告就没这张图"，也不再跟别人抢号。范围拆分会把整份引用清单复制给每一侧，
 * 人工把不属于自己的引用摘掉后计数必须跟着降，否则冲突永远消不掉。
 *
 * 照片也要算进来的原因：defect_photos.photo_number 上只有普通索引、没有唯一约束，
 * 两条病害各挂一张同编号的图会一路写进报告，让编号这个交叉引用作废——只数引用条目
 * 看不见这种重号。
 */
function countPhotoNumberClaims(draft: BridgeAnnualInspectionData): Map<string, number> {
  const linkedNumbers = new Map<string, Set<string>>();
  for (const photo of draft.photos) {
    const defectId = photo.linked_defect_candidate_id;
    if (!defectId) continue;
    const numbers = linkedNumbers.get(defectId);
    if (numbers) numbers.add(photo.photo_number);
    else linkedNumbers.set(defectId, new Set([photo.photo_number]));
  }
  const counts = new Map<string, number>();
  for (const defect of draft.defects) {
    if (defect.review_status === "已忽略") continue;
    const claimed = new Set(linkedNumbers.get(defect.candidate_id) ?? []);
    for (const reference of defect.photo_references) {
      if (reference.resolution === "missing") continue;
      claimed.add(reference.photo_number);
    }
    for (const number of claimed) {
      counts.set(number, (counts.get(number) ?? 0) + 1);
    }
  }
  return counts;
}

export function buildDefectPhotoReviewModel(
  input: DefectPhotoReviewModelInput,
): DefectPhotoReviewModel {
  const ratingTreeNodes = new Map(
    (input.ratingTreeNodes ?? []).map((node) => [node.id, node] as const),
  );
  const ratingTreeNodeSummaries = new Map(
    (input.ratingTreeNodeSummaries ?? []).map((node) => [node.id, node] as const),
  );
  const repeatedPhotoNumbers = new Set(
    [...countPhotoNumberClaims(input.draft)]
      .filter(([, count]) => count > 1)
      .map(([number]) => number),
  );
  const resolution = input.resolution ?? EMPTY_RESOLUTION_INDEX;
  const allRows = input.draft.defects.map((defect) =>
    analyzeDefect(
      input.draft,
      defect,
      resolutionOf(resolution, defect.candidate_id),
      input.assessmentIssues,
      repeatedPhotoNumbers,
      input.ratingTreeVersionId,
      ratingTreeNodes,
      ratingTreeNodeSummaries,
      input.applicableTreeNodeIdsByComponent ?? new Map(),
      input.treeRulesReady ?? false,
      input.matchResults?.get(defect.candidate_id) ?? null,
    ),
  );
  const search = input.search?.trim().toLocaleLowerCase() ?? "";
  // 部件顺序是主键：整份列表按 板 → 铰缝 → 支座 → 墩柱 → … 一条顺下来，
  // 像照着纸质报告逐部件核对。同一部件内部才轮到"要不要人工判断"的优先级。
  //
  // 没绑定构件的排在最后：它们还不属于任何部件，插在中间会打断走查。顶部那几个
  // 统计页签本身就是筛选，要集中处理它们点一下就行，不必靠排序顶上来。
  const componentOrder = input.componentOrder;
  const partRank = (row: DefectReviewRow): number => {
    if (!componentOrder) return 0;  // 顺序还没取到：整体退回优先级排序
    const componentId = resolutionOf(resolution, row.defect.candidate_id).bridgeComponentId;
    if (!componentId) return Number.MAX_SAFE_INTEGER;
    return componentOrder.get(componentId) ?? Number.MAX_SAFE_INTEGER;
  };
  const ordered = allRows
    .map((row, index) => ({ row, index }))
    .sort((left, right) => {
      const part = partRank(left.row) - partRank(right.row);
      if (part !== 0) return part;
      const rank = sortRank(left.row) - sortRank(right.row);
      return rank !== 0 ? rank : left.index - right.index;
    })
    .map((item) => item.row);
  // 各部件多少条，按走查顺序给（ordered 已经排好）。下拉直接照它渲染：
  // 只列这份草稿里真的出现过的部件，空部件不占位。
  const partCounts = new Map<string, number>();
  for (const row of ordered) {
    const componentId = resolutionOf(resolution, row.defect.candidate_id).bridgeComponentId;
    const name = componentId ? input.componentPart?.get(componentId) : undefined;
    const key = name ?? UNBOUND_PART_FILTER;
    partCounts.set(key, (partCounts.get(key) ?? 0) + 1);
  }
  const summary = {
    parts: [...partCounts.entries()].map(([name, count]) => ({ name, count })),
    all: allRows.length,
    pending: allRows.filter((row) => row.status === "needs_attention").length,
    batchable: allRows.filter((row) => row.status === "batchable").length,
    confirmed: allRows.filter((row) => row.status === "confirmed").length,
    composite: allRows.filter((row) => row.matchState === "composite").length,
    candidates: allRows.filter((row) => row.matchState === "candidates").length,
    unmatched: allRows.filter((row) => row.matchState === "unmatched").length,
  };
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
    if (input.partFilter) {
      const componentId = resolutionOf(resolution, row.defect.candidate_id).bridgeComponentId;
      const name = componentId ? input.componentPart?.get(componentId) : undefined;
      if ((name ?? UNBOUND_PART_FILTER) !== input.partFilter) return false;
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
