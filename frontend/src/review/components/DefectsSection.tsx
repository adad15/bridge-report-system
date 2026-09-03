import { Spin } from "antd";
import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties, type Dispatch, type FormEvent, type KeyboardEvent as ReactKeyboardEvent, type PointerEvent as ReactPointerEvent } from "react";
import {
  EMPTY_RESOLUTION_INDEX,
  buildResolutionIndex,
  mergeResolutionIndex,
  resolutionOf,
  type ResolutionIndex,
} from "../resolutionIndex";
import { addManualDefect, fetchResolutionWorkspace } from "../../api/resolutionApi";
import {
  CheckCircleOutlined,
  FileProtectOutlined,
  FileSearchOutlined,
  PictureOutlined,
  SafetyCertificateOutlined,
} from "@ant-design/icons";

import type { AssessmentIssue } from "../../api/assessmentApi";
import { componentInventoryErrorMessage, fetchComponentReviewOrder, fetchInventorySummary, searchInventoryEntries, type ComponentInventoryEntry, type InventorySummary, type StructurePart as InventoryStructurePart } from "../../api/componentInventoryApi";
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
import { ApiError } from "../../api/apiClient";
import { applySourceRatingResolution } from "../../api/resolutionApi";
import { applicableRatingTreeNodes } from "../applicableRatingTreeNodes";
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
  onSave?: () => void;
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
const DETAIL_WIDTH_STORAGE_KEY = "bridge-report:defect-detail-width-percent-v2";
const DEFAULT_DETAIL_WIDTH = 73;
const MIN_DETAIL_WIDTH = 58;
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
export function DefectsSection({ draft, importRecordId, baseUrl, bridgeId, selectedCandidateId, selectedPhotoCandidateId, onSelect, onCloseDetail, onSave, dispatch, ratingTree = null, assessmentIssues = EMPTY_ASSESSMENT_ISSUES, disabled = false, allowStructureChanges = false, editLockToken = null, isDefectEditable }: DefectsSectionProps) {
  const [showAddForm, setShowAddForm] = useState(false);
  // 构件绑定与评分树节点 5.0 起住在关系表里，由工作区读模型提供；草稿版本供手工新增
  // 的 If-Match 使用。两者一起来自同一个响应，不会各自过期。
  const [resolution, setResolution] = useState<ResolutionIndex>(EMPTY_RESOLUTION_INDEX);
  const [resolutionReady, setResolutionReady] = useState(false);
  /** 首屏取不回解析状态时的原因。留空表示正常。 */
  const [resolutionError, setResolutionError] = useState<string | null>(null);
  const [draftVersion, setDraftVersion] = useState(1);
  const [inventoryRevisionId, setInventoryRevisionId] = useState<string | null>(null);

  /* 这一份重取会被并发发起好几次（挂载、绑定、评分树裁决、批量应用各一次），
     而单次要 0.6~3 秒。响应回来的顺序跟发出的顺序无关：先发的后到，就会把新状态
     盖回旧的。刚导入完那一下最明显——最早那次取到的是后端还没写完解析的快照，
     它要是最后落地，整页就显示成"一条都没绑"，刷新（只发一次）又好了。
     所以按发起序号只认最后一次，迟到的旧响应直接丢弃。 */
  const resolutionRequest = useRef(0);
  /* 是否成功取回过一次。用 ref 而不是读 resolution.size：refreshResolution 的依赖里
     没有 resolution，闭包会把它永远捕获成初始的空 Map，后续失败就会误报成首屏失败。 */
  const resolutionLoadedOnce = useRef(false);

  // 解析状态的唯一来源。绑定、评分树选择、批量应用之后都重新拉一次——写操作只回
  // 受影响对象，整份重取才是这一页保持一致的最省心做法（几百个组一次请求）。
  const refreshResolution = useCallback(async () => {
    const seq = ++resolutionRequest.current;
    try {
      const workspace = await fetchResolutionWorkspace(baseUrl, importRecordId);
      if (seq !== resolutionRequest.current) return;
      resolutionLoadedOnce.current = true;
      setResolutionError(null);
      setResolution(buildResolutionIndex(workspace));
      setDraftVersion(workspace.draft_version);
      setInventoryRevisionId(workspace.inventory_revision_id);
    } catch (caught) {
      if (seq !== resolutionRequest.current) return;
      /* 保持上一份：清空会让整页突然显示成"一条都没绑"。
         但首屏那次失败时"上一份"本来就是空的，静默吞掉等于把 279 条全渲染成未绑定
         ——看着像数据丢了，其实只是没取回来。所以第一次失败必须说出来。 */
      if (!resolutionLoadedOnce.current) {
        setResolutionError(caught instanceof ApiError
          ? caught.message
          : "取不到构件绑定与评定结果，请刷新重试。");
      }
    } finally {
      // 已被更新的请求接替时不动 ready：那一次会自己负责收尾。
      if (seq === resolutionRequest.current) setResolutionReady(true);
    }
  }, [baseUrl, importRecordId]);

  useEffect(() => { void refreshResolution(); }, [refreshResolution]);
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
  const [componentOrder, setComponentOrder] = useState<Map<string, number> | null>(null);
  const [componentPart, setComponentPart] = useState<Map<string, string>>(new Map());
  const [partFilter, setPartFilter] = useState<string | null>(null);
  const [defectTypeFilter, setDefectTypeFilter] = useState<string | null>(null);
  const [treeError, setTreeError] = useState("");
  const [filter, setFilter] = useState<DefectReviewFilter>("all");
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
  /* 正在详情栏里打开的那一条，钉在列表里不随筛选消失。
     选完评定树节点、补完照片、确认之后，这条就不再命中"待处理"了；不钉住的话它会
     当场从列表里消失，看起来像"选一下就自动确认了"。用户主动换筛选 / 搜索 / 翻页 /
     切视图时才放开——那是明确的离开动作。 */
  const [pinnedRowId, setPinnedRowId] = useState<string | null>(null);
  const seenSafeIds = useRef(new Set<string>());
  const splitWorkspaceRef = useRef<HTMLDivElement>(null);
  const selectedTreeNodeIds = useMemo(
    () => [...new Set(
      draft.defects
        .map((defect) => resolutionOf(resolution, defect.candidate_id).ratingTreeNodeId)
        .filter((id): id is string => Boolean(id)),
    )].sort(),
    [draft.defects, resolution],
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
  //
  // 逐个活动实例取构件，而不是取"这条病害的单一构件"：区间展开的病害一条挂多件，
  // 单一 id 按约定为 null，按它过滤会把展开出来的构件整批漏掉——它们进不了适用节点表，
  // 于是这些病害的节点全被判成"不适用于当前实际构件"。
  const boundComponentKey = useMemo(
    () => [...new Set(draft.defects
      .flatMap((defect) => resolutionOf(resolution, defect.candidate_id).components)
      .map((item) => `${item.componentId}\u0000${item.categoryId}`))]
      .sort().join("|"),
    [draft.defects, resolution],
  );

  // 走查顺序：后端按 结构部位 → 部件目录次序 → 台账 sort_order 排好，前端只按下标摆行。
  // 依赖用内容键而不是 draft.defects 的引用——后者每次渲染都是新数组，会让这里反复重取。
  useEffect(() => {
    const ids = [...new Set(draft.defects
      .flatMap((defect) => resolutionOf(resolution, defect.candidate_id).componentIds))];
    if (ids.length === 0) { setComponentOrder(new Map()); return; }
    let cancelled = false;
    fetchComponentReviewOrder(baseUrl, bridgeId, ids)
      .then((ordered) => {
        if (cancelled) return;
        setComponentOrder(new Map(ordered.map((item, index) => [item.bridge_component_id, index])));
        setComponentPart(new Map(ordered.map((item) => [item.bridge_component_id, item.part_name])));
      })
      // 取不到就退回纯优先级排序：列表照常可用，只是不按部件走。
      .catch(() => { if (!cancelled) setComponentOrder(null); });
    return () => { cancelled = true; };
  }, [baseUrl, bridgeId, boundComponentKey, draft.defects]);

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
        // 全部实例构件，不是"单一构件"——区间展开的病害那一项恒为 null，
        // 按它取签名就不会变，绑定改了也不会重新匹配。
        resolutionOf(resolution, defect.candidate_id).componentIds.join(","),
        defect.review_status,
        defect.group_review_status,
      ].join("|"))
      .join("~"),
    [draft.defects, resolution],
  );
  const lastMatchSignature = useRef<string | null>(null);

  const runMatch = useCallback(async (candidateIds?: string[]) => {
    if (!ratingTree || draft.defects.length === 0) return;
    setRematching(true);
    try {
      // 几百条病害只发这一个请求；后端只算不写，页面拿到结果后再落进本地草稿。
      const report = await matchDefectRatingTreeNodes(
        baseUrl, importRecordId, draft.defects, resolution, candidateIds,
      );
      setMatchResults((current) => {
        const next = candidateIds ? new Map(current) : new Map<string, DefectMatchResult>();
        for (const result of report.results) next.set(result.candidate_id, result);
        return next;
      });
      setMatchSummary(report.summary);
      setMatchedAt(new Date());
      setMatchError(null);
      // 这个接口只算不写（DefectMatchingRoutes 用可确认视图做只读计算），所以算完不必
      // 重取工作区——关系态不可能因为这次调用而变。此前这里按 auto_bound 触发一次整份
      // 重取，取回来的必然与手上的一样。
      //
      // 权威的自动匹配结果由导入初始化与区间展开等写命令落库；页面这里拿到的是"按当前
      // 草稿重算的话会是什么"，供人参考，不是已经生效的状态。
    } catch (error) {
      // 服务失败不能伪装成"这批病害都没有匹配结果"：清掉上一轮结果并显式报错。
      setMatchResults(new Map());
      setMatchSummary(null);
      setMatchedAt(null);
      setMatchError(defectMatchErrorMessage(error));
    } finally {
      setRematching(false);
    }
    /* refreshResolution 不在依赖里：上面那段注释说明了这个接口只算不写，算完不必重取
       工作区，函数体早已不用它。留着它会让 refreshResolution 每变一次就重建 runMatch，
       进而触发下面那个自动匹配 effect——而 refreshResolution 一完成就 setResolution，
       又反过来重建 runMatch。这条自激链正是同一个接口被连发好几次的来源之一。 */
  }, [baseUrl, draft.defects, importRecordId, ratingTree, resolution]);

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
    resolution,
    componentOrder: componentOrder ?? undefined,
    componentPart,
    assessmentIssues,
    matchResults,
  }), [applicableTreeNodeIdsByComponent, assessmentIssues, draft, matchResults, ratingTree?.version_id, ratingTreeNodeSummaries, resolution, treeNodeDetails, treeRulesReady, componentOrder, componentPart]);
  const visibleModel = useMemo(() => buildDefectPhotoReviewModel({
    draft,
    ratingTreeVersionId: ratingTree?.version_id ?? null,
    ratingTreeNodes: treeNodeDetails,
    ratingTreeNodeSummaries,
    applicableTreeNodeIdsByComponent,
    treeRulesReady,
    resolution,
    componentOrder: componentOrder ?? undefined,
    componentPart,
    assessmentIssues,
    matchResults,
    filter,
    issueFilter,
    search,
    partFilter,
    defectTypeFilter,
  }), [applicableTreeNodeIdsByComponent, assessmentIssues, draft, filter, issueFilter, matchResults, ratingTree?.version_id, ratingTreeNodeSummaries, resolution, search, treeNodeDetails, treeRulesReady, componentOrder, componentPart, partFilter, defectTypeFilter]);

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
    if (!currentRow || pinnedRowId !== currentRow.candidateId) return visibleModel.rows;
    if (visibleModel.rows.some((row) => row.candidateId === currentRow.candidateId)) return visibleModel.rows;
    const order = new Map(allModel.rows.map((row, index) => [row.candidateId, index]));
    const currentIndex = order.get(currentRow.candidateId) ?? Number.MAX_SAFE_INTEGER;
    const insertionIndex = visibleModel.rows.findIndex(
      (row) => (order.get(row.candidateId) ?? Number.MAX_SAFE_INTEGER) > currentIndex,
    );
    const rows = [...visibleModel.rows];
    rows.splice(insertionIndex < 0 ? rows.length : insertionIndex, 0, currentRow);
    return rows;
  }, [allModel.rows, currentRow, pinnedRowId, visibleModel.rows]);
  // 重新匹配默认作用于当前筛选范围内的未确认记录；人工与已确认结果由后端跳过。
  const rematchCandidateIds = useMemo(
    () => visibleModel.rows
      .filter((row) => row.status !== "confirmed" && row.status !== "ignored")
      .map((row) => row.candidateId),
    [visibleModel.rows],
  );
  const rematchScopeLabel =
    filter === "all" && !issueFilter && !search && !partFilter && !defectTypeFilter
      ? "全部"
      : "当前筛选";
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
  const linkedPhotoCount = draft.photos.filter((photo) => photo.linked_defect_candidate_id).length;
  const qualityChecksPending = !resolutionReady || Boolean(ratingTree && !treeRulesReady) || rematching;
  const selectedEntry = inventoryEntries.find((entry) => entry.id === form.componentEntryId);
  const selectedMapping = selectedEntry?.mappings.find((item) => item.is_active);
  const manualDefectNodes = useMemo(
    () => sortRatingTreeNodes(selectedEntry
      ? treeNodesByComponent.get(selectedEntry.bridge_component_id) ?? []
      : []),
    [selectedEntry, treeNodesByComponent],
  );

  const closeDetail = () => {
    setPinnedRowId(null);
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

  const clearPinnedResult = () => setPinnedRowId(null);
  const openIssueGroups = () => {
    clearPinnedResult();
    setFilter("needs_attention");
    setIssueFilter(null);
    setSearch("");
    setPartFilter(null);
    setReviewMode("groups");
    onCloseDetail?.();
  };

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
    if (!editLockToken) {
      setFormError("需要编辑权才能新增病害。");
      return;
    }
    // 5.0：手工新增走专用命令。用户在这个对话框里选定的**构件与评分树节点**必须原样
    // 保住——退回"建组 + 自动匹配"是把明确的点选降级成一次猜测，节点那一半几乎必然
    // 丢失（自动匹配只在唯一命中时才写）。
    void (async () => {
      try {
        const created = await addManualDefect(
          baseUrl,
          importRecordId,
          {
            bridge_component_id: entry.bridge_component_id,
            rating_tree_node_id: treeNode.id,
            defect_facts: {
              defect_type: treeNode.display_name,
              defect_location: location,
              defect_description: description,
              defect_scale: treeNode.is_scoring ? scale : null,
            },
            expected_inventory_revision_id: inventory.id,
          },
          draftVersion,
          editLockToken,
        );
        // 新候选与新版本必须**一起**并进本地状态：只更新版本却保留缺少新候选的旧草稿，
        // 下一次整份保存就会把它当成"用户删掉了"。
        dispatch({
          type: "replace_draft",
          data: {
            ...draft,
            defects: [...draft.defects, created.source_defect as never],
          },
        });
        setDraftVersion(created.draft_version);
        // 并进去而不是整份替换；评定树版本跟着一起给，否则新病害会被当成
        // "节点版本对不上"。
        setResolution((previous) => mergeResolutionIndex(previous, {
          groups: created.result.affected_groups as never,
          rating_tree: ratingTree ? { version_id: ratingTree.version_id } : null,
        }));
        setForm(EMPTY_MANUAL_DEFECT);
        setShowAddForm(false);
        setFormError("");
      } catch (error) {
        setFormError(error instanceof Error ? error.message : "新增病害失败。");
      }
    })();
  };

  /* 关系表没回来时，每条病害都会渲染成"未绑定构件、未定评定项"——那不是校对结论，
     是数据还没到。整页在此之前不出，免得把加载中态读成"匹配全掉了"。

     只等 resolutionReady，不等 treeRulesReady：评定树规则迟到是另一回事，那时页面
     照常出，计数显示「—」表示"还不知道"（见工具栏 countsPending）。把规则也纳进闸门
     会让那套刻意的表达再也走不到，而且取规则失败时 treeRulesReady 永远为 false，
     界面会卡在转圈上连错误都看不到。 */
  /* 解析状态一条都没取回来时，不能照常渲染：那会把每条病害都显示成"未绑定构件、
     未定评定项"，看着像数据丢了。说清楚是没取回来，而不是真的没绑。 */
  if (resolutionError && resolution.size === 0) {
    return (
      <section className="status-panel defect-photo-section">
        <p className="error-text" role="alert">未能取到构件绑定与评定结果：{resolutionError}</p>
        <p>页面上的病害条目暂时无法显示绑定与评定状态。请刷新页面重试。</p>
      </section>
    );
  }

  if (!resolutionReady) {
    return (
      <section className="status-panel defect-photo-section defect-section-loading" aria-busy="true">
        <Spin size="large" />
        <p>正在载入病害与照片…</p>
      </section>
    );
  }

  return (
    <section className="status-panel defect-photo-section">
      <div className="defect-overview-metrics" aria-label="病害与照片汇总">
        <article className="defect-metric-card blue">
          <span className="defect-metric-icon"><FileProtectOutlined /></span>
          <span><small>病害总数</small><strong>{allModel.summary.all}</strong><em>条</em></span>
        </article>
        <article className="defect-metric-card green">
          <span className="defect-metric-icon"><SafetyCertificateOutlined /></span>
          <span><small>已确认</small><strong>{allModel.rows.filter((row) => row.defect.group_review_status === "已确认").length}</strong><em>条</em></span>
        </article>
        <article className="defect-metric-card blue">
          <span className="defect-metric-icon"><PictureOutlined /></span>
          <span><small>关联照片</small><strong>{linkedPhotoCount}</strong><em>张</em></span>
        </article>
        <button
          type="button"
          className={`defect-metric-card issue-entry ${qualityChecksPending ? "blue" : allModel.summary.pending > 0 ? "orange" : "green"}`}
          disabled={qualityChecksPending || allModel.summary.pending === 0}
          onClick={openIssueGroups}
        >
          <span className="defect-metric-icon">{qualityChecksPending || allModel.summary.pending > 0 ? <FileSearchOutlined /> : <CheckCircleOutlined />}</span>
          <span>
            <small>{qualityChecksPending ? "正在检查" : allModel.summary.pending > 0 ? "待处理问题" : "校对通过"}</small>
            <strong>{qualityChecksPending ? "—" : allModel.summary.pending}</strong><em>条</em>
          </span>
        </button>
      </div>
      <div className={`defect-review-layout ${currentRow ? "has-detail" : ""}`}>
        <div className="defect-review-card">
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
            partFilter={partFilter}
            onPartFilterChange={setPartFilter}
            defectTypeFilter={defectTypeFilter}
            onDefectTypeFilterChange={setDefectTypeFilter}
            rematchScopeLabel={rematchScopeLabel}
            rematchCount={rematchCandidateIds.length}
            rematching={rematching}
            matchError={matchError}
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
              setPinnedRowId(candidateId);
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
                setPinnedRowId(candidateId);
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
                  applicableNodes={applicableRatingTreeNodes(currentRow.resolution.componentIds, treeNodesByComponent)}
                  importRecordId={importRecordId}
                  baseUrl={baseUrl}
                  bridgeId={bridgeId}
                  initialPhotoCandidateId={selectedPhotoCandidateId}
                  dispatch={dispatch}
                  disabled={disabled || (isDefectEditable !== undefined && !isDefectEditable(currentRow.defect))}
                  allowDelete={allowStructureChanges}
                  editLockToken={editLockToken}
                  onSave={onSave}
                  onClose={closeDetail}
                  onDefectTextCommitted={(candidateId) => { void runMatch([candidateId]); }}
                  onRatingResolved={() => { void refreshResolution(); }}
                  inventoryRevisionId={inventoryRevisionId}
                  onConfirm={() => {
                    // 确认后停在原地：自动跳下一条会让人来不及看确认结果，
                    // 想回头核对还得自己找回来。要看下一条由用户自己点。
                    setPinnedRowId(currentRow.candidateId);
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
        </div>
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
            setPinnedRowId(selectedCandidateId);
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
          setPendingGroupAssignment(null);
          if (!editLockToken) {
            setTreeError("当前页面没有编辑权，批量应用未保存。");
            return;
          }
          // 权威节点写关系表，草稿只跟着改连带的来源事实。此前这里只 dispatch 改草稿：
          // 病害名字改了、节点没存，刷新后名字还在节点没了——比不生效更难发现。
          //
          // 先写后端、成功了再改本地。反过来的话，一次失败就留下一份"看着已应用、其实
          // 没落库"的草稿，而用户还能把它保存进去。
          void (async () => {
            setTreeError("");
            let applied = 0;
            for (const row of group.rows) {
              const instances = row.resolution.instances;
              if (instances.length === 0) {
                setTreeError(
                  `已应用 ${applied} 条；“${row.defect.component_number ?? row.candidateId}”` +
                  "还没绑定实际构件，本组其余病害未应用。");
                if (applied > 0) void refreshResolution();
                return;
              }
              try {
                await applySourceRatingResolution(
                  baseUrl,
                  importRecordId,
                  row.candidateId,
                  {
                    instances: instances.map((instance) => ({
                      instance_id: instance.instanceId,
                      expected_version: instance.ratingVersion,
                    })),
                    rating_tree_node_id: node.id,
                    expected_rating_tree_version_id: ratingTree.version_id,
                    ...(inventoryRevisionId
                      ? { expected_inventory_revision_id: inventoryRevisionId } : {}),
                  },
                  editLockToken);
              } catch (caught) {
                // 一组里每一行是各自独立的一条来源病害，写到第几条就是第几条——这是
                // 真实状态，不是半个写入。说清楚停在哪儿，让工作区重取把实情摆出来。
                setTreeError(
                  `已应用 ${applied} 条，第 ${applied + 1} 条失败：` +
                  (caught instanceof ApiError ? caught.message : "请刷新后重试。"));
                if (applied > 0) void refreshResolution();
                return;
              }
              applied += 1;
            }
            dispatch({
              type: "select_rating_tree_nodes",
              candidateIds: group.rows.map((row) => row.candidateId),
              versionId: ratingTree.version_id,
              nodeId: node.id,
              nodeName: node.display_name,
              isScoring: node.is_scoring,
              matchEvidence: "用户按相同来源身份批量指定评定树病害",
            });
            void refreshResolution();
          })();
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
