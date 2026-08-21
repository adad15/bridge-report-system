import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties, type Dispatch, type FormEvent, type KeyboardEvent as ReactKeyboardEvent, type PointerEvent as ReactPointerEvent } from "react";

import type { AssessmentIssue } from "../../api/assessmentApi";
import { componentInventoryErrorMessage, fetchInventorySummary, searchInventoryEntries, type ComponentInventoryEntry, type InventorySummary, type StructurePart as InventoryStructurePart } from "../../api/componentInventoryApi";
import {
  defectMatchErrorMessage,
  matchDefectRatingTreeNodes,
  type DefectMatchResult,
  type DefectMatchSummary,
} from "../../api/defectMatchingApi";
import {
  fetchApplicableRatingTreeDefects,
  fetchRatingTreeNode,
  ratingTreeErrorMessage,
  type RatingTreeNode,
  type RatingTreeNodeSummary,
} from "../../api/ratingTreeApi";
import type { ReviewRatingTree } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData, DefectCandidate } from "../../contracts/annualInspection";
import {
  ratingTreeDisplayLabel,
  ratingTreeOptionLabel,
  sortRatingTreeNodes,
} from "../../rating-tree/ratingTreeLabels";
import { buildDefectPhotoReviewModel, type DefectReviewFilter, type DefectReviewIssueFilter } from "../defectPhotoReviewModel";
import { buildDefectIssueGroups, type DefectIssueGroup } from "../defectIssueGroups";
import type { ReviewDraftAction } from "../reviewDraft";
import { DefectBatchAssignDialog } from "./DefectBatchAssignDialog";
import { DefectBatchConfirmDialog } from "./DefectBatchConfirmDialog";
import { DefectDetailEditor } from "./DefectDetailEditor";
import { DefectIssueGroupConfirmDialog } from "./DefectIssueGroupConfirmDialog";
import { DefectIssueGroupList } from "./DefectIssueGroupList";
import { DefectQuickReviewList } from "./DefectQuickReviewList";
import { DefectReviewToolbar } from "./DefectReviewToolbar";
import { UnlinkedPhotosPanel } from "./UnlinkedPhotosPanel";

interface DefectsSectionProps {
  draft: BridgeAnnualInspectionData;
  importRecordId: string;
  baseUrl: string;
  bridgeId: string;
  selectedCandidateId: string | null;
  onSelect: (candidateId: string, photoCandidateId?: string) => void;
  onCloseDetail?: () => void;
  dispatch: Dispatch<ReviewDraftAction>;
  ratingTree?: ReviewRatingTree | null;
  assessmentIssues?: AssessmentIssue[];
  selectedPhotoCandidateId?: string | null;
  disabled?: boolean;
  allowStructureChanges?: boolean;
  /** 上传补充照片要带编辑锁令牌；没有令牌时上传入口自动关掉。 */
  editLockToken?: string | null;
  /**
   * 逐病害可编辑判定（重开校对 warnings_only 态下仅带警告的病害可改）。
   * 不传视为全部可编辑；与 disabled 叠加：disabled=true 时全部不可编辑。
   */
  isDefectEditable?: (defect: DefectCandidate) => boolean;
}

const EMPTY_ASSESSMENT_ISSUES: AssessmentIssue[] = [];
const DETAIL_WIDTH_STORAGE_KEY = "bridge-report:defect-detail-width-percent";
const DEFAULT_DETAIL_WIDTH = 66.67;
const MIN_DETAIL_WIDTH = 45;
const MAX_DETAIL_WIDTH = 85;

function readStoredDetailWidth(): number {
  try {
    const stored = Number(window.localStorage.getItem(DETAIL_WIDTH_STORAGE_KEY));
    if (Number.isFinite(stored) && stored >= MIN_DETAIL_WIDTH && stored <= MAX_DETAIL_WIDTH) return stored;
  } catch {
    // localStorage may be unavailable in restricted browser contexts.
  }
  return DEFAULT_DETAIL_WIDTH;
}

const STRUCTURE_PART_LABELS: Record<InventoryStructurePart, "全桥" | "上部结构" | "下部结构" | "桥面系" | "其他"> = {
  overall: "全桥",
  superstructure: "上部结构",
  substructure: "下部结构",
  deck_system: "桥面系",
  other: "其他",
};

interface ManualDefectFormState {
  componentEntryId: string;
  defectLocation: string;
  ratingTreeNodeId: string;
  defectDescription: string;
  defectScale: string;
}

const EMPTY_MANUAL_DEFECT: ManualDefectFormState = {
  componentEntryId: "",
  defectLocation: "",
  ratingTreeNodeId: "",
  defectDescription: "",
  defectScale: "",
};

// 禁用策略按详情控件处理，不用 fieldset disabled 一揽子禁用；
// 筛选、翻页、缩略图等只读动作在已确认记录中仍可使用。
export function DefectsSection({ draft, importRecordId, baseUrl, bridgeId, selectedCandidateId, selectedPhotoCandidateId, onSelect, onCloseDetail, dispatch, ratingTree = null, assessmentIssues = EMPTY_ASSESSMENT_ISSUES, disabled = false, allowStructureChanges = false, editLockToken = null, isDefectEditable }: DefectsSectionProps) {
  const [showAddForm, setShowAddForm] = useState(false);
  // 搜索命中的构件（选中的那条也留在这里），不再是整份台账。
  const [inventoryEntries, setInventoryEntries] = useState<ComponentInventoryEntry[]>([]);
  // 首屏只要这份分组汇总：修订版 id 与每个类别的 (桥型, 规范类别) 都在里面，而它不含
  // 构件明细。此前为了这两样东西要下载整份台账（现网一座桥 3.6 MB / 5174 条构件）。
  const [inventorySummary, setInventorySummary] = useState<InventorySummary | null>(null);
  const [loadingInventory, setLoadingInventory] = useState(false);
  const [componentSearch, setComponentSearch] = useState("");
  const [componentSearching, setComponentSearching] = useState(false);
  const [formError, setFormError] = useState("");
  const [form, setForm] = useState<ManualDefectFormState>(EMPTY_MANUAL_DEFECT);
  const [treeNodesByComponent, setTreeNodesByComponent] =
    useState<Map<string, RatingTreeNodeSummary[]>>(new Map());
  const [treeNodeDetails, setTreeNodeDetails] = useState<RatingTreeNode[]>([]);
  const loadedTreeNodeIds = useRef(new Set<string>());
  const loadingTreeNodeIds = useRef(new Set<string>());
  const treeNodeDetailsVersion = useRef<string | null>(null);
  const [manualTreeNode, setManualTreeNode] = useState<RatingTreeNode | null>(null);
  const [treeRulesReady, setTreeRulesReady] = useState(false);
  const [treeError, setTreeError] = useState("");
  const [filter, setFilter] = useState<DefectReviewFilter>("needs_attention");
  const [issueFilter, setIssueFilter] = useState<DefectReviewIssueFilter | null>(null);
  const [search, setSearch] = useState("");
  const [matchResults, setMatchResults] = useState<Map<string, DefectMatchResult>>(new Map());
  const [matchSummary, setMatchSummary] = useState<DefectMatchSummary | null>(null);
  const [matchedAt, setMatchedAt] = useState<Date | null>(null);
  const [matchError, setMatchError] = useState<string | null>(null);
  const [rematching, setRematching] = useState(false);
  const [selectedIds, setSelectedIds] = useState<Set<string>>(new Set());
  const [batchDialogOpen, setBatchDialogOpen] = useState(false);
  const [reviewMode, setReviewMode] = useState<"records" | "groups">("records");
  const [pendingGroupAssignment, setPendingGroupAssignment] = useState<{
    group: DefectIssueGroup;
    node: RatingTreeNodeSummary;
  } | null>(null);
  const [pendingGroupConfirmationKey, setPendingGroupConfirmationKey] = useState<string | null>(null);
  const [detailWidth, setDetailWidth] = useState(readStoredDetailWidth);
  const [pinnedConfirmedId, setPinnedConfirmedId] = useState<string | null>(null);
  const seenSafeIds = useRef(new Set<string>());
  const splitWorkspaceRef = useRef<HTMLDivElement>(null);
  const selectedTreeNodeIds = useMemo(
    () => [...new Set(
      draft.defects
        .map((defect) => defect.rating_tree_node_id)
        .filter((id): id is string => Boolean(id)),
    )].sort(),
    [draft.defects],
  );
  const selectedTreeNodeIdsKey = selectedTreeNodeIds.join("\u0000");

  useEffect(() => {
    if (!ratingTree || !form.ratingTreeNodeId) {
      setManualTreeNode(null);
      return;
    }
    let cancelled = false;
    void fetchRatingTreeNode(baseUrl, ratingTree.version_id, form.ratingTreeNodeId)
      .then((node) => {
        if (!cancelled) setManualTreeNode(node);
      })
      .catch((error) => {
        if (!cancelled) setFormError(ratingTreeErrorMessage(error));
      });
    return () => {
      cancelled = true;
    };
  }, [baseUrl, form.ratingTreeNodeId, ratingTree]);

  // 汇总在首屏就要：每一行的评定树病害选择器都依赖它算出的 (桥型, 规范类别)，
  // 不只是手动添加表单。它不含构件明细，一座 5174 构件的桥也只有十几行。
  useEffect(() => {
    if (!ratingTree || inventorySummary) return;
    let cancelled = false;
    fetchInventorySummary(baseUrl, bridgeId)
      .then((summary) => { if (!cancelled) setInventorySummary(summary); })
      .catch(() => { /* 汇总取不到时评定树规则不可用，下面的 treeError 会说明 */ });
    return () => { cancelled = true; };
  }, [baseUrl, bridgeId, ratingTree, inventorySummary]);

  // 草稿里出现过的 (构件, 规范类别) 组合。用内容做依赖而不是 draft.defects 的引用——
  // 后者每次渲染都是新数组，会让下面那个副作用反复重跑并把 treeRulesReady 打回 false。
  const boundComponentKey = useMemo(
    () => [...new Set(draft.defects
      .filter((defect) => defect.bridge_component_id && defect.standard_component_category_id)
      .map((defect) => `${defect.bridge_component_id}\u0000${defect.standard_component_category_id}`))]
      .sort().join("|"),
    [draft.defects],
  );

  // 每个构件适用哪些评定树病害节点，只取决于它的 (桥型, 规范类别)。分组汇总里就有
  // 这一对（每个类别一行，本桥 18 行），而每条已绑定病害的 JSON 里也带着
  // standard_component_category_id——两者一拼就够了，不必为此下载整份台账。
  useEffect(() => {
    if (!ratingTree || !inventorySummary) {
      setTreeNodesByComponent(new Map());
      setTreeRulesReady(Boolean(!ratingTree));
      return;
    }
    let cancelled = false;
    setTreeRulesReady(false);
    setTreeError("");
    const scopes = new Map<string, { bridgeTypeId: string; componentCategoryId: string }>();
    for (const group of inventorySummary.groups) {
      // 类别与桥型同出一条生效映射，同为 null 或同非 null。
      if (!group.standard_component_category_id || !group.standard_bridge_type_id) continue;
      scopes.set(group.standard_component_category_id, {
        bridgeTypeId: group.standard_bridge_type_id,
        componentCategoryId: group.standard_component_category_id,
      });
    }
    void Promise.all([...scopes.entries()].map(async ([key, scope]) => [
      key,
      await fetchApplicableRatingTreeDefects(
        baseUrl,
        ratingTree.version_id,
        scope.bridgeTypeId,
        scope.componentCategoryId,
      ),
    ] as const))
      .then((scopeResults) => {
        if (cancelled) return;
        const byCategory = new Map(scopeResults);
        const byComponent = new Map<string, RatingTreeNodeSummary[]>();
        // 草稿里已绑定的病害：构件 id 与规范类别都写在病害自己身上。
        for (const pair of boundComponentKey ? boundComponentKey.split("|") : []) {
          const [componentId, categoryId] = pair.split("\u0000");
          byComponent.set(componentId, byCategory.get(categoryId) ?? []);
        }
        // 手动添加表单里搜到的构件：它们的映射随搜索结果一起回来了。
        for (const entry of inventoryEntries) {
          const mapping = entry.mappings.find((item) => item.is_active);
          if (!mapping) continue;
          byComponent.set(entry.bridge_component_id,
            byCategory.get(mapping.standard_component_category_id) ?? []);
        }
        setTreeNodesByComponent(byComponent);
        setTreeRulesReady(true);
      })
      .catch((error) => {
        if (!cancelled) {
          setTreeError(ratingTreeErrorMessage(error));
          setTreeRulesReady(false);
        }
    });
    return () => { cancelled = true; };
  }, [baseUrl, inventorySummary, inventoryEntries, boundComponentKey, ratingTree]);

  useEffect(() => {
    if (!ratingTree) {
      setTreeNodeDetails([]);
      loadedTreeNodeIds.current.clear();
      loadingTreeNodeIds.current.clear();
      treeNodeDetailsVersion.current = null;
      return;
    }
    if (treeNodeDetailsVersion.current !== ratingTree.version_id) {
      setTreeNodeDetails([]);
      loadedTreeNodeIds.current.clear();
      loadingTreeNodeIds.current.clear();
      treeNodeDetailsVersion.current = ratingTree.version_id;
    }
    const missingIds = selectedTreeNodeIds.filter(
      (nodeId) => !loadedTreeNodeIds.current.has(nodeId) && !loadingTreeNodeIds.current.has(nodeId),
    );
    if (missingIds.length === 0) return;
    const requestedVersionId = ratingTree.version_id;
    for (const nodeId of missingIds) loadingTreeNodeIds.current.add(nodeId);
    void Promise.all(missingIds.map((nodeId) =>
      fetchRatingTreeNode(baseUrl, requestedVersionId, nodeId)))
      .then((details) => {
        if (treeNodeDetailsVersion.current !== requestedVersionId) return;
        for (const node of details) {
          loadingTreeNodeIds.current.delete(node.id);
          loadedTreeNodeIds.current.add(node.id);
        }
        setTreeNodeDetails((current) => {
          const next = new Map(current.map((node) => [node.id, node] as const));
          for (const node of details) next.set(node.id, node);
          return [...next.values()];
        });
      })
      .catch((error) => {
        if (treeNodeDetailsVersion.current !== requestedVersionId) return;
        for (const nodeId of missingIds) loadingTreeNodeIds.current.delete(nodeId);
        setTreeError(ratingTreeErrorMessage(error));
      });
  }, [baseUrl, ratingTree, selectedTreeNodeIdsKey]);

  const applicableTreeNodeIdsByComponent = useMemo(
    () => new Map(
      [...treeNodesByComponent.entries()].map(([componentId, nodes]) => [
        componentId,
        new Set(nodes.map((node) => node.id)),
      ]),
    ),
    [treeNodesByComponent],
  );

  const nodeSummaryById = useMemo(() => {
    const map = new Map<string, RatingTreeNodeSummary>();
    for (const nodes of treeNodesByComponent.values()) {
      for (const node of nodes) map.set(node.id, node);
    }
    return map;
  }, [treeNodesByComponent]);
  const ratingTreeNodeSummaries = useMemo(
    () => [...nodeSummaryById.values()],
    [nodeSummaryById],
  );

  // 自动触发的去重签名只看依赖类变化（构件绑定、病害增删、复核状态）。
  // 病害类型与描述属于输入过程，改它们不在这里发请求，改由字段失焦提交触发，
  // 避免逐键请求。写回自动结果不会改变签名，
  // 所以"应用结果 -> 重新触发"不会变成死循环。
  const matchInputSignature = useMemo(
    () => draft.defects
      .map((defect) => [
        defect.candidate_id,
        defect.bridge_component_id ?? "",
        defect.review_status,
        defect.group_review_status,
      ].join("|"))
      .join("~"),
    [draft.defects],
  );
  const lastMatchSignature = useRef<string | null>(null);

  const runMatch = useCallback(async (candidateIds?: string[]) => {
    if (!ratingTree || draft.defects.length === 0) return;
    setRematching(true);
    try {
      // 几百条病害只发这一个请求；后端只算不写，页面拿到结果后再落进本地草稿。
      const report = await matchDefectRatingTreeNodes(
        baseUrl, importRecordId, draft.defects, candidateIds,
      );
      setMatchResults((current) => {
        const next = candidateIds ? new Map(current) : new Map<string, DefectMatchResult>();
        for (const result of report.results) next.set(result.candidate_id, result);
        return next;
      });
      setMatchSummary(report.summary);
      setMatchedAt(new Date());
      setMatchError(null);
      const autoMatches = report.results
        .filter((result) => !result.skipped && result.outcome === "auto_bound" && result.rating_tree_node_id)
        .map((result) => ({
          candidateId: result.candidate_id,
          nodeId: result.rating_tree_node_id!,
          matchMethod: result.match_method ?? "exact",
          matchEvidence: result.match_evidence ?? "系统自动匹配",
          isScoring: nodeSummaryById.get(result.rating_tree_node_id!)?.is_scoring ?? true,
        }));
      if (autoMatches.length > 0) {
        dispatch({
          type: "apply_rating_tree_auto_matches",
          versionId: report.rating_tree_version_id,
          matches: autoMatches,
        });
      }
    } catch (error) {
      // 服务失败不能伪装成"这批病害都没有匹配结果"：清掉上一轮结果并显式报错。
      setMatchResults(new Map());
      setMatchSummary(null);
      setMatchedAt(null);
      setMatchError(defectMatchErrorMessage(error));
    } finally {
      setRematching(false);
    }
  }, [baseUrl, dispatch, draft.defects, importRecordId, nodeSummaryById, ratingTree]);

  // 自动触发：导入、构件绑定、评定树绑定完成，或未确认病害的构件/类型/描述改动后
  // 各触发一次。输入过程中不请求，短时间内的重复变化合并成一次。
  useEffect(() => {
    if (!ratingTree || draft.defects.length === 0) return;
    const signature = `${ratingTree.version_id}~${matchInputSignature}`;
    if (lastMatchSignature.current === signature) return;
    const timer = window.setTimeout(() => {
      lastMatchSignature.current = signature;
      void runMatch();
    }, 300);
    return () => window.clearTimeout(timer);
  }, [draft.defects.length, matchInputSignature, ratingTree, runMatch]);

  const allModel = useMemo(() => buildDefectPhotoReviewModel({
    draft,
    ratingTreeVersionId: ratingTree?.version_id ?? null,
    ratingTreeNodes: treeNodeDetails,
    ratingTreeNodeSummaries,
    applicableTreeNodeIdsByComponent,
    treeRulesReady,
    assessmentIssues,
    matchResults,
  }), [applicableTreeNodeIdsByComponent, assessmentIssues, draft, matchResults, ratingTree?.version_id, ratingTreeNodeSummaries, treeNodeDetails, treeRulesReady]);
  const visibleModel = useMemo(() => buildDefectPhotoReviewModel({
    draft,
    ratingTreeVersionId: ratingTree?.version_id ?? null,
    ratingTreeNodes: treeNodeDetails,
    ratingTreeNodeSummaries,
    applicableTreeNodeIdsByComponent,
    treeRulesReady,
    assessmentIssues,
    matchResults,
    filter,
    issueFilter,
    search,
  }), [applicableTreeNodeIdsByComponent, assessmentIssues, draft, filter, issueFilter, matchResults, ratingTree?.version_id, ratingTreeNodeSummaries, search, treeNodeDetails, treeRulesReady]);

  useEffect(() => {
    setSelectedIds((current) => {
      const next = new Set([...current].filter((id) => allModel.safeCandidateIds.has(id)));
      for (const id of allModel.safeCandidateIds) {
        if (!seenSafeIds.current.has(id)) next.add(id);
        seenSafeIds.current.add(id);
      }
      if (next.size === current.size && [...next].every((id) => current.has(id))) return current;
      return next;
    });
  }, [allModel.safeCandidateIds]);

  const currentRow = selectedCandidateId
    ? allModel.rows.find((row) => row.candidateId === selectedCandidateId) ?? null
    : null;
  const displayedRows = useMemo(() => {
    if (!currentRow || pinnedConfirmedId !== currentRow.candidateId) return visibleModel.rows;
    if (visibleModel.rows.some((row) => row.candidateId === currentRow.candidateId)) return visibleModel.rows;
    const order = new Map(allModel.rows.map((row, index) => [row.candidateId, index]));
    const currentIndex = order.get(currentRow.candidateId) ?? Number.MAX_SAFE_INTEGER;
    const insertionIndex = visibleModel.rows.findIndex(
      (row) => (order.get(row.candidateId) ?? Number.MAX_SAFE_INTEGER) > currentIndex,
    );
    const rows = [...visibleModel.rows];
    rows.splice(insertionIndex < 0 ? rows.length : insertionIndex, 0, currentRow);
    return rows;
  }, [allModel.rows, currentRow, pinnedConfirmedId, visibleModel.rows]);
  // 重新匹配默认作用于当前筛选范围内的未确认记录；人工与已确认结果由后端跳过。
  const rematchCandidateIds = useMemo(
    () => visibleModel.rows
      .filter((row) => row.status !== "confirmed" && row.status !== "ignored")
      .map((row) => row.candidateId),
    [visibleModel.rows],
  );
  const rematchScopeLabel =
    filter === "all" && !issueFilter && !search ? "全部" : "当前筛选";
  const currentlySafeSelection = [...selectedIds].filter((id) => allModel.safeCandidateIds.has(id));
  const selectableCandidateIds = useMemo(
    () => visibleModel.rows
      .filter((row) => row.batchEligible)
      .map((row) => row.candidateId),
    [visibleModel.rows],
  );
  const selectedSelectableCount = selectableCandidateIds.filter((id) => selectedIds.has(id)).length;
  const allSelectableSelected =
    selectableCandidateIds.length > 0 && selectedSelectableCount === selectableCandidateIds.length;
  const someSelectableSelected =
    selectedSelectableCount > 0 && !allSelectableSelected;
  const issueGroups = useMemo(
    () => buildDefectIssueGroups(visibleModel.rows),
    [visibleModel.rows],
  );
  const pendingGroupConfirmation = pendingGroupConfirmationKey
    ? issueGroups.find((group) => group.key === pendingGroupConfirmationKey) ?? null
    : null;
  const batchDistribution = useMemo(() => {
    const counts = new Map<string, number>();
    for (const row of allModel.rows) {
      if (!currentlySafeSelection.includes(row.candidateId)) continue;
      const name =
        (row.ratingTreeNode ? ratingTreeDisplayLabel(row.ratingTreeNode) : null) ??
        (row.defect.defect_type || "未确定规范病害");
      counts.set(name, (counts.get(name) ?? 0) + 1);
    }
    return [...counts.entries()]
      .map(([name, count]) => ({ name, count }))
      .sort((left, right) => right.count - left.count || left.name.localeCompare(right.name));
    // currentlySafeSelection 每次渲染都是新数组，用它的内容做依赖而不是引用。
  }, [allModel.rows, currentlySafeSelection.join("|")]);
  const selectedPhotoCount = draft.photos.filter(
    (photo) => photo.linked_defect_candidate_id && currentlySafeSelection.includes(photo.linked_defect_candidate_id),
  ).length;
  const selectedEntry = inventoryEntries.find((entry) => entry.id === form.componentEntryId);
  const selectedMapping = selectedEntry?.mappings.find((item) => item.is_active);
  const manualDefectNodes = useMemo(
    () => sortRatingTreeNodes(selectedEntry
      ? treeNodesByComponent.get(selectedEntry.bridge_component_id) ?? []
      : []),
    [selectedEntry, treeNodesByComponent],
  );

  const closeDetail = () => {
    setPinnedConfirmedId(null);
    onCloseDetail?.();
  };

  useEffect(() => {
    if (!currentRow || batchDialogOpen) return;
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key !== "Escape" || event.defaultPrevented) return;
      closeDetail();
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [batchDialogOpen, currentRow, onCloseDetail]);

  const updateDetailWidth = (nextWidth: number) => {
    const clamped = Math.min(MAX_DETAIL_WIDTH, Math.max(MIN_DETAIL_WIDTH, nextWidth));
    setDetailWidth(clamped);
    try {
      window.localStorage.setItem(DETAIL_WIDTH_STORAGE_KEY, String(clamped));
    } catch {
      // The current session still uses the resized width.
    }
  };

  const startResize = (event: ReactPointerEvent<HTMLDivElement>) => {
    if (!splitWorkspaceRef.current) return;
    event.preventDefault();
    const workspace = splitWorkspaceRef.current;
    event.currentTarget.setPointerCapture(event.pointerId);
    const onPointerMove = (moveEvent: PointerEvent) => {
      const bounds = workspace.getBoundingClientRect();
      if (bounds.width <= 0) return;
      updateDetailWidth(((bounds.right - moveEvent.clientX) / bounds.width) * 100);
    };
    const finish = () => {
      window.removeEventListener("pointermove", onPointerMove);
      window.removeEventListener("pointerup", finish);
      window.removeEventListener("pointercancel", finish);
    };
    window.addEventListener("pointermove", onPointerMove);
    window.addEventListener("pointerup", finish);
    window.addEventListener("pointercancel", finish);
  };

  const resizeWithKeyboard = (event: ReactKeyboardEvent<HTMLDivElement>) => {
    if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
    event.preventDefault();
    updateDetailWidth(detailWidth + (event.key === "ArrowLeft" ? 2 : -2));
  };

  const clearPinnedResult = () => setPinnedConfirmedId(null);

  const openAddForm = async () => {
    setShowAddForm(true);
    setFormError("");
    setComponentSearch("");
    setInventoryEntries([]);
    setForm((current) => ({ ...current, componentEntryId: "" }));
    if (inventorySummary) return;   // 首屏已取；构件本身由搜索按需取
    setLoadingInventory(true);
    try {
      setInventorySummary(await fetchInventorySummary(baseUrl, bridgeId));
    } catch (error) {
      setFormError(componentInventoryErrorMessage(error));
    } finally {
      setLoadingInventory(false);
    }
  };

  // 构件选择改成按需检索。原先是把整份台账灌进一个 <select>：现网一座桥 5174 个
  // <option>，其中前 3300 个全是支座，要选"3#墩盖梁"得在原生下拉里滚过三千多行。
  useEffect(() => {
    if (!showAddForm) return;
    const revisionId = inventorySummary?.revision.id;
    const term = componentSearch.trim();
    if (!revisionId || !term) {
      setInventoryEntries([]);
      return;
    }
    const controller = new AbortController();
    const timer = setTimeout(() => {
      setComponentSearching(true);
      searchInventoryEntries(baseUrl, revisionId, term, 20, controller.signal, true)
        .then((response) => {
          setInventoryEntries(response.entries);
          setFormError(response.total === 0 ? "没有匹配的构件。" : "");
        })
        .catch((error) => {
          if (controller.signal.aborted) return;   // 主动取消不是错误
          setInventoryEntries([]);
          setFormError(componentInventoryErrorMessage(error));
        })
        .finally(() => {
          if (!controller.signal.aborted) setComponentSearching(false);
        });
    }, 250);
    return () => {
      controller.abort();
      clearTimeout(timer);
    };
  }, [showAddForm, componentSearch, inventorySummary, baseUrl]);

  const submitManualDefect = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const entry = inventoryEntries.find((item) => item.id === form.componentEntryId);
    const inventory = inventorySummary?.revision ?? null;
    const mapping = entry?.mappings.find((item) => item.is_active);
    const treeNode = manualDefectNodes.find((item) => item.id === form.ratingTreeNodeId);
    const location = form.defectLocation.trim();
    const description = form.defectDescription.trim();
    const scale = form.defectScale === "" ? null : Number(form.defectScale);
    if (!inventory || !entry || !mapping || !treeNode || !ratingTree || !location || !description) {
      setFormError("请填写构件类别、构件编号、病害位置、病害类型和病害描述。");
      return;
    }
    if (scale !== null && (!Number.isInteger(scale) || scale <= 0)) {
      setFormError("病害标度必须是正整数，也可以暂时留空。");
      return;
    }
    if (scale !== null && manualTreeNode?.is_scoring && !manualTreeNode.allowed_scales.includes(scale)) {
      setFormError("病害标度不在该评定树节点允许范围内。");
      return;
    }
    dispatch({
      type: "add_defect",
      input: {
        componentName: entry.site_component_type,
        componentNumber: entry.component_number,
        bridgeComponentId: entry.bridge_component_id,
        standardComponentCategoryId: mapping.standard_component_category_id,
        resolvedStructurePart: STRUCTURE_PART_LABELS[mapping.structure_part],
        inventoryRevisionId: inventory.id,
        defectLocation: location,
        defectType: treeNode.display_name,
        ratingTreeVersionId: ratingTree.version_id,
        ratingTreeNodeId: treeNode.id,
        defectDescription: description,
        defectScale: scale,
        isScoring: treeNode.is_scoring,
      },
    });
    setForm(EMPTY_MANUAL_DEFECT);
    setShowAddForm(false);
    setFormError("");
  };

  return (
    <section className="status-panel defect-photo-section">
      {/* 分区标题现在长在工具条里：两者本来就要一起吸顶，拆成两个 sticky 元素只会
          在中间留一道能透出滚动内容的缝，还得拿伪元素去补。 */}
      <DefectReviewToolbar
        summary={allModel.summary}
        filter={filter}
        issueFilter={issueFilter}
        search={search}
        selectedCount={currentlySafeSelection.length}
        selectableCount={selectableCandidateIds.length}
        allSelectableSelected={allSelectableSelected}
        someSelectableSelected={someSelectableSelected}
        viewMode={reviewMode}
        issueGroupCount={issueGroups.length}
        disabled={disabled}
        countsPending={!treeRulesReady}
        matchCountsPending={matchSummary === null && matchError === null
          && draft.defects.length > 0}
        rematchScopeLabel={rematchScopeLabel}
        rematchCount={rematchCandidateIds.length}
        rematching={rematching}
        matchError={matchError}
        lastMatchSummary={matchSummary}
        lastMatchAt={matchedAt}
        onAddDefect={openAddForm}
        addDefectDisabled={!allowStructureChanges || loadingInventory}
        onFilterChange={(nextFilter) => { clearPinnedResult(); setFilter(nextFilter); }}
        onIssueFilterChange={(nextIssueFilter) => { clearPinnedResult(); setIssueFilter(nextIssueFilter); }}
        onSearchChange={(nextSearch) => { clearPinnedResult(); setSearch(nextSearch); }}
        onToggleSelectAll={() => setSelectedIds((current) => {
          const next = new Set(current);
          if (allSelectableSelected) {
            for (const id of selectableCandidateIds) next.delete(id);
          } else {
            for (const id of selectableCandidateIds) next.add(id);
          }
          return next;
        })}
        onViewModeChange={(mode) => {
          clearPinnedResult();
          setReviewMode(mode);
          if (mode === "groups") onCloseDetail?.();
        }}
        onBatchConfirm={() => setBatchDialogOpen(true)}
        onRematch={() => { void runMatch(rematchCandidateIds); }}
      />
      {showAddForm ? (
        <form className="manual-defect-form" onSubmit={submitManualDefect}>
          <label>搜索构件<input aria-label="搜索构件" placeholder="编号、类别或现场名，如 3#墩盖梁" disabled={loadingInventory} value={componentSearch} onChange={(event) => setComponentSearch(event.target.value)} /></label>
          <label>实际构件<select aria-label="实际构件" disabled={loadingInventory || inventoryEntries.length === 0} required value={form.componentEntryId} onChange={(event) => setForm({ ...form, componentEntryId: event.target.value, ratingTreeNodeId: "" })}><option value="">{componentSearching ? "正在搜索…" : inventoryEntries.length === 0 ? "先在上方搜索构件" : "请选择构件"}</option>{inventoryEntries.map((entry) => <option key={entry.id} value={entry.id}>{entry.component_number} / {entry.site_component_type} / {entry.site_name}</option>)}</select></label>
          <label>构件类别<input aria-label="新增病害构件类别" readOnly value={inventoryEntries.find((entry) => entry.id === form.componentEntryId)?.site_component_type ?? ""} /></label>
          <label>构件编号<input aria-label="新增病害构件编号" readOnly value={inventoryEntries.find((entry) => entry.id === form.componentEntryId)?.component_number ?? ""} /></label>
          <label>病害位置<input aria-label="新增病害位置" required value={form.defectLocation} onChange={(event) => setForm({ ...form, defectLocation: event.target.value })} /></label>
          <label>病害类型<select aria-label="新增病害类型" required value={form.ratingTreeNodeId} onChange={(event) => setForm({ ...form, ratingTreeNodeId: event.target.value, defectScale: "" })}><option value="">请选择评定树病害</option>{manualDefectNodes.map((node) => <option key={node.id} value={node.id}>{ratingTreeOptionLabel(node, manualDefectNodes)}{node.is_scoring ? "" : "（暂不计分）"}</option>)}</select></label>
          <label className="manual-defect-form-wide">病害描述<input aria-label="新增病害描述" required value={form.defectDescription} onChange={(event) => setForm({ ...form, defectDescription: event.target.value })} /></label>
          <label>病害标度（可稍后填写）<select aria-label="新增病害标度" disabled={!manualTreeNode?.is_scoring} value={form.defectScale} onChange={(event) => setForm({ ...form, defectScale: event.target.value })}><option value="">{manualTreeNode?.is_scoring ? "请选择标度" : "该节点暂不计分"}</option>{manualTreeNode?.allowed_scales.map((scale) => <option key={scale} value={scale}>{scale} · {manualTreeNode.scale_descriptions[String(scale)]}</option>)}</select></label>
          {formError ? <p className="form-error" role="alert">{formError}</p> : null}
          <div className="manual-defect-form-actions"><button type="button" onClick={() => { setShowAddForm(false); setFormError(""); }}>取消</button><button type="submit" disabled={loadingInventory || !form.componentEntryId}>添加病害</button></div>
        </form>
      ) : null}
      {/* 原来这里是一个 fieldset：它曾经用 disabled 一揽子关掉整片区域，禁用改成
          逐控件处理后就只剩一个空壳，还带着 fieldset 自己的 min-width:min-content。 */}
      <div className="defect-review-body">
        {draft.defects.length === 0 ? <p>暂无病害候选，可使用“新增病害”手动添加。</p> : null}
        {treeError ? <p className="form-error" role="alert">{treeError}</p> : null}
        {!ratingTree ? <p className="warning-text">当前检测年度未锁定评定树，无法确定病害评分节点。</p> : null}
        {reviewMode === "groups" ? (
          <DefectIssueGroupList
            groups={issueGroups}
            nodesByComponent={treeNodesByComponent}
            disabled={disabled || !ratingTree}
            onApplyNode={(group, node) => setPendingGroupAssignment({ group, node })}
            onConfirmGroup={(group) => setPendingGroupConfirmationKey(group.key)}
            onOpenDefect={(candidateId) => {
              setReviewMode("records");
              clearPinnedResult();
              onSelect(candidateId);
            }}
          />
        ) : (
          <div
            ref={splitWorkspaceRef}
            className={`defect-review-workspace ${currentRow ? "detail-open" : ""}`}
            style={{ "--defect-detail-width": `${detailWidth}%` } as CSSProperties}
          >
          <div className="defect-review-list-pane">
            <DefectQuickReviewList
              rows={displayedRows}
              importRecordId={importRecordId}
              baseUrl={baseUrl}
              selectedCandidateIds={selectedIds}
              activeCandidateId={selectedCandidateId}
              compact={Boolean(currentRow)}
              onPageChange={clearPinnedResult}
              onToggleSelection={(candidateId) => setSelectedIds((current) => {
                const next = new Set(current);
                if (next.has(candidateId)) next.delete(candidateId);
                else if (allModel.safeCandidateIds.has(candidateId)) next.add(candidateId);
                return next;
              })}
              onOpen={(candidateId, photoCandidateId) => {
                clearPinnedResult();
                onSelect(candidateId, photoCandidateId);
              }}
            />
          </div>
          {currentRow ? (
            <>
              <div
                className="defect-detail-resize-handle"
                role="separator"
                aria-label="调整精细维护区域宽度"
                aria-orientation="vertical"
                aria-valuemin={MIN_DETAIL_WIDTH}
                aria-valuemax={MAX_DETAIL_WIDTH}
                aria-valuenow={Math.round(detailWidth)}
                tabIndex={0}
                onPointerDown={startResize}
                onKeyDown={resizeWithKeyboard}
              />
              <aside className="defect-detail-pane">
                <DefectDetailEditor
                  draft={draft}
                  row={currentRow}
                  ratingTreeVersionId={ratingTree?.version_id ?? null}
                  applicableNodes={treeNodesByComponent.get(currentRow.defect.bridge_component_id ?? "") ?? []}
                  importRecordId={importRecordId}
                  baseUrl={baseUrl}
                  initialPhotoCandidateId={selectedPhotoCandidateId}
                  dispatch={dispatch}
                  disabled={disabled || (isDefectEditable !== undefined && !isDefectEditable(currentRow.defect))}
                  allowDelete={allowStructureChanges}
                  editLockToken={editLockToken}
                  onClose={closeDetail}
                  onDefectTextCommitted={(candidateId) => { void runMatch([candidateId]); }}
                  onConfirm={() => {
                    setPinnedConfirmedId(currentRow.candidateId);
                    dispatch({ type: "confirm_defect_groups", candidateIds: [currentRow.candidateId] });
                  }}
                />
              </aside>
            </>
          ) : null}
          </div>
        )}
        <UnlinkedPhotosPanel draft={draft} importRecordId={importRecordId} baseUrl={baseUrl} selectedPhotoCandidateId={selectedPhotoCandidateId} />
      </div>
      <DefectBatchConfirmDialog
        open={batchDialogOpen}
        defectCount={currentlySafeSelection.length}
        photoCount={selectedPhotoCount}
        removedCount={selectedIds.size - currentlySafeSelection.length}
        defectNameDistribution={batchDistribution}
        onCancel={() => setBatchDialogOpen(false)}
        onConfirm={() => {
          const validIds = [...selectedIds].filter((id) => allModel.safeCandidateIds.has(id));
          if (selectedCandidateId && validIds.includes(selectedCandidateId)) {
            setPinnedConfirmedId(selectedCandidateId);
          }
          if (validIds.length > 0) dispatch({ type: "confirm_defect_groups", candidateIds: validIds });
          setBatchDialogOpen(false);
        }}
      />
      <DefectBatchAssignDialog
        assignment={pendingGroupAssignment}
        onCancel={() => setPendingGroupAssignment(null)}
        onConfirm={() => {
          if (!pendingGroupAssignment || !ratingTree) return;
          const { group, node } = pendingGroupAssignment;
          dispatch({
            type: "select_rating_tree_nodes",
            candidateIds: group.rows.map((row) => row.candidateId),
            versionId: ratingTree.version_id,
            nodeId: node.id,
            nodeName: node.display_name,
            isScoring: node.is_scoring,
            matchEvidence: "用户按相同来源身份批量指定评定树病害",
          });
          setPendingGroupAssignment(null);
        }}
      />
      <DefectIssueGroupConfirmDialog
        group={pendingGroupConfirmation}
        onCancel={() => setPendingGroupConfirmationKey(null)}
        onConfirm={() => {
          if (!pendingGroupConfirmation) return;
          const candidateIds = pendingGroupConfirmation.rangeSplitConfirmableRows.map(
            (row) => row.candidateId,
          );
          if (candidateIds.length > 0) {
            dispatch({ type: "confirm_defect_groups", candidateIds });
          }
          setPendingGroupConfirmationKey(null);
        }}
      />
    </section>
  );
}
