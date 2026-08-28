import { useEffect, useMemo, useRef, useState } from "react";
import {
  applyComponentResolution,
  applyResolutionPlan,
  createResolutionPlan,
  fetchResolutionWorkspace,
  INVENTORY_REVISION_CHANGED,
  type ResolutionPlanPreview,
} from "../../api/resolutionApi";
import {
  bindingProgress,
  toBindingOverview,
  type BindingComponentSummary,
  type BindingRow,
  type ComponentBindingOverview,
} from "./bindingViewModel";

import {
  searchInventoryEntries,
  type ComponentInventoryEntry,
} from "../../api/componentInventoryApi";
import { bindInspectionRatingTree } from "../../api/importBindingApi";

/** 区间展开的选中项。group_id 是计划的定位键，后两项仅供展示。 */
interface SplitTarget {
  group_id: string;
  part_name: string;
  component_number: string;
}
import {
  fetchRatingTreeVersions,
  type RatingTreeVersionSummary,
} from "../../api/ratingTreeApi";
import { BulkReplaceDialog } from "./BulkReplaceDialog";
import { ComponentRangeSplitDialog } from "./ComponentRangeSplitDialog";
import { ApiError } from "../../api/apiClient";
import { backendBaseUrl } from "../../config";
import "./ComponentBindingRatingTree.css";

const MAX_SEARCH_RESULTS = 20;

const STATUS_LABELS: Record<string, string> = {
  bound: "已绑定",
  unmatched: "未匹配",
  ambiguous: "歧义",
  missing: "已标记缺失",
};

type BindingFilter = "pending" | "bound" | "missing" | "all";

/**
 * 范围拆分的错误里，后端会在 details 带回卡住整批的那个目标（部件 + 编号）。
 * 只报一句"本次拆分生成的病害数量超过上限。"的话，用户看不到是哪一行顶破了上限，
 * 而拆分是整批判定、超限时连预览都出不来，只能一个个取消勾选去试。
 */
function rejectedTargetHint(details: unknown): string {
  if (typeof details !== "object" || details === null) return "";
  const target = details as { part_name?: unknown; component_number?: unknown };
  if (typeof target.part_name !== "string" || typeof target.component_number !== "string") {
    return "";
  }
  return `（问题出在「${target.part_name} · ${target.component_number}」）`;
}

function errorMessage(caught: unknown): string {
  if (caught instanceof ApiError) {
    if (caught.code === "component_binding_conflict") {
      return "台账未确认、导入不在待校对阶段，或所选构件与该部件类别不符。";
    }
    return caught.message + rejectedTargetHint(caught.details);
  }
  return "绑定操作失败，请稍后重试。";
}

function isRevisionChanged(caught: unknown): boolean {
  return caught instanceof ApiError && caught.code === INVENTORY_REVISION_CHANGED;
}

// 契约不变量：inventory_confirmed 为真时 inventory_revision_id 必然非空，而整块绑定 UI
// 都在 inventory_confirmed 之后。真取到空值说明后端违反了契约，就地抛错比带着 null
// 发请求、等后端回 400 更容易定位。
function requireRevisionId(overview: ComponentBindingOverview): string {
  if (!overview.inventory_revision_id) {
    throw new Error("构件台账版本缺失，请刷新页面后重试。");
  }
  return overview.inventory_revision_id;
}

function splitTargetKey(partName: string, componentNumber: string): string {
  return `${partName}\n${componentNumber}`;
}

function GroupSplitSelector({
  partName,
  eligibleRows,
  selection,
  busy,
  onChange,
}: {
  partName: string;
  eligibleRows: BindingRow[];
  selection: Map<string, SplitTarget>;
  busy: boolean;
  onChange: (checked: boolean) => void;
}) {
  const checkbox = useRef<HTMLInputElement>(null);
  const selectedCount = eligibleRows.filter((row) =>
    selection.has(splitTargetKey(partName, row.component_number))
  ).length;
  const allSelected = eligibleRows.length > 0 && selectedCount === eligibleRows.length;
  // 行数说明不了规模：一行 "1-1#梁~1-25#梁" 带 3 条病害，展开就是 75 条。
  // 拆分有 2000 条的整批上限，一键全选很容易越界，所以把预计条数摆在按钮旁边。
  const projectedDefects = eligibleRows.reduce(
    (sum, row) => sum + row.defect_count * (row.split_expanded_count ?? 0),
    0
  );

  useEffect(() => {
    if (checkbox.current) {
      checkbox.current.indeterminate = selectedCount > 0 && !allSelected;
    }
  }, [selectedCount, allSelected]);

  return (
    <label className="binding-group-split-select">
      <input
        ref={checkbox}
        type="checkbox"
        aria-label={`全选 ${partName} 待拆分构件`}
        checked={allSelected}
        disabled={busy}
        onChange={(event) => onChange(event.target.checked)}
      />
      <span>{allSelected ? "取消全选" : "全选待拆分"} {eligibleRows.length}</span>
      {projectedDefects > 0 ? (
        <span className="binding-split-projection">约 {projectedDefects} 条</span>
      ) : null}
    </label>
  );
}

/**
 * 候选与搜索结果来自两个来源，字段名不同（概览是 entry_id，检索返回的是 id）。
 * 混进同一个下拉时 <option key> 会取到 undefined，React 会报重复 key 并错误复用节点，
 * 所以两边都先转成这一种模型再合并。
 */
interface BindingComponentOption {
  bridgeComponentId: string;
  componentNumber: string;
  siteComponentType: string;
  siteName: string;
  candidate: boolean;
}

function optionFromSummary(summary: BindingComponentSummary): BindingComponentOption {
  return {
    bridgeComponentId: summary.bridge_component_id,
    componentNumber: summary.component_number,
    siteComponentType: summary.site_component_type,
    siteName: summary.site_name,
    candidate: true,
  };
}

function optionFromEntry(entry: ComponentInventoryEntry): BindingComponentOption {
  return {
    bridgeComponentId: entry.bridge_component_id,
    componentNumber: entry.component_number,
    siteComponentType: entry.site_component_type,
    siteName: entry.site_name,
    candidate: false,
  };
}

// 下拉的 value 平时是 bridge_component_id；"两侧"选项要绑两件，塞不进一个 id，
// 所以用一个不可能与 UUID 相撞的哨兵值，选中后按 side_pair_option 里的 id 走。
const SIDE_PAIR_VALUE = "__side_pair__";

interface RowActionProps {
  row: BindingRow;
  revisionId: string;
  busy: boolean;
  onBind: (bridgeComponentId: string) => void;
  onBindMulti: (bridgeComponentIds: string[]) => void;
  onMarkMissing: () => void;
  onClear: () => void;
}

function RowAction(
  { row, revisionId, busy, onBind, onBindMulti, onMarkMissing, onClear }: RowActionProps
) {
  const [search, setSearch] = useState("");
  const [results, setResults] = useState<BindingComponentOption[]>([]);
  const [searchError, setSearchError] = useState<string | null>(null);

  // 行内搜索改走服务端：整份台账有五千多条，为了这个下拉把它整个下载下来正是本次
  // 要去掉的形态。防抖避免逐字发请求；AbortController 保证慢的旧响应覆盖不了新的。
  useEffect(() => {
    const term = search.trim();
    if (!term) {
      setResults([]);
      setSearchError(null);
      return;
    }
    const controller = new AbortController();
    const timer = setTimeout(() => {
      searchInventoryEntries(
        backendBaseUrl, revisionId, term, MAX_SEARCH_RESULTS, controller.signal, true)
        .then((response) => {
          setResults(response.entries.map(optionFromEntry));
          setSearchError(null);
        })
        .catch((caught) => {
          if (controller.signal.aborted) return;   // 主动取消不是错误
          setSearchError(errorMessage(caught));
        });
    }, 250);
    return () => {
      controller.abort();
      clearTimeout(timer);
    };
  }, [search, revisionId]);

  if (row.status === "bound") {
    const bound = row.bound_component;
    // 绝大多数行都是按同名精确匹配绑上的，重复显示一遍同样的编号只是噪声——
    // "已绑定"徽标已经说明状态。只有绑到了别的编号（人工改绑）才值得标出来。
    const rebound = bound && bound.component_number !== row.component_number;
    return (
      <div className="binding-row-action">
        {rebound ? (
          <span className="binding-bound-target">
            → {bound.component_number} / {bound.site_component_type}
          </span>
        ) : null}
        {!bound ? <span className="binding-bound-target">已绑定构件</span> : null}
        <button type="button" disabled={busy} aria-label={`取消绑定 ${row.component_number}`} onClick={onClear}>
          取消绑定
        </button>
      </div>
    );
  }
  if (row.status === "missing") {
    return (
      <div className="binding-row-action">
        <span className="binding-missing-note">台账确无此构件</span>
        <button type="button" disabled={busy} aria-label={`取消标记 ${row.component_number}`} onClick={onClear}>
          取消标记
        </button>
      </div>
    );
  }

  const term = search.trim();
  // 候选始终在前且保持概览给的顺序，搜索结果追加在后，按构件去重。
  // limit 只约束服务端返回条数，候选另计——候选被搜索结果挤掉是旧实现的毛病。
  const options = new Map<string, BindingComponentOption>();
  for (const candidate of row.candidate_components) {
    options.set(candidate.bridge_component_id, optionFromSummary(candidate));
  }
  for (const result of results) {
    if (!options.has(result.bridgeComponentId)) options.set(result.bridgeComponentId, result);
  }

  return (
    <div className="binding-row-action">
      <input
        aria-label={`搜索实际构件 ${row.component_number}`}
        placeholder="编号、类别或现场名"
        value={search}
        disabled={busy}
        onChange={(event) => setSearch(event.target.value)}
      />
      <select
        aria-label={`为 ${row.component_number} 选择实际构件`}
        value=""
        disabled={busy}
        onChange={(event) => {
          const value = event.target.value;
          if (!value) return;
          if (value === SIDE_PAIR_VALUE) {
            // 哨兵值不是构件 id，必须在这里分流；漏了就会把 "__side_pair__"
            // 当成 bridge_component_id 发给单条绑定接口。
            const option = row.side_pair_option;
            if (option) onBindMulti(option.bridge_component_ids);
            return;
          }
          onBind(value);
        }}
      >
        <option value="">
          {term && options.size === 0 ? "没有匹配的构件" : "请选择实际构件（可先搜索）"}
        </option>
        {/* "两侧"排在候选之上：报告写"两侧护栏"时，逐个绑左右两件才是对的做法，
            单选任一侧都会让另一侧留在满分。 */}
        {row.side_pair_option ? (
          <option value={SIDE_PAIR_VALUE}>{row.side_pair_option.label}</option>
        ) : null}
        {[...options.values()].map((option) => (
          <option key={option.bridgeComponentId} value={option.bridgeComponentId}>
            {option.candidate ? "候选 · " : ""}
            {option.componentNumber} / {option.siteComponentType} / {option.siteName}
          </option>
        ))}
      </select>
      {/* 搜索失败只报在行内，概览带来的候选项照常可用。 */}
      {searchError ? <span className="binding-row-error">{searchError}</span> : null}
      <button type="button" disabled={busy} aria-label={`标记缺失 ${row.component_number}`} onClick={onMarkMissing}>
        标记缺失
      </button>
    </div>
  );
}

export function ComponentBindingWorkspace({
  importId,
  bridgeId,
  lockToken,
  onEnterReview,
  onOverviewChange,
  onRatingTreeChange,
  onDraftInvalidated,
}: {
  importId: string;
  bridgeId: string;
  /**
   * 编辑锁令牌，未持有编辑权时为 null。后端六个绑定写接口现在都要求持锁——
   * 它们改的是 import_records.parsed_result_json，与校对草稿保存写的是同一份数据，
   * 不持锁写进去的修改会被持锁者的整份保存覆盖掉。
   *
   * 概览是只读的，没有编辑权照样能看，所以这里允许为 null；写操作由
   * requireLockToken() 在下手前挡住，与校对页保存草稿同一处置。
   */
  lockToken: string | null;
  onEnterReview?: () => void;
  // 每次拿到新的概览（首次加载与每次绑定操作后）都上报，供校对页侧栏同步待处理计数。
  onOverviewChange?: (overview: ComponentBindingOverview) => void;
  // 评定树也是病害与评定分区的年度上下文，绑定后让父页面重取完整校对数据。
  onRatingTreeChange?: () => void;
  /**
   * 绑定、批量替换、标记缺失、取消绑定和范围拆分都会由后端改写
   * parsed_result_json（拆分还会增删病害与照片关系）。父页面的草稿是首屏拉取后
   * 独立持有的 reducer 状态，不重取就会一直显示拆分前的旧病害，之后保存还会
   * 把旧内容盖回去。这里在每次成功的写操作后上报一次，让父页面按需重取。
   */
  onDraftInvalidated?: () => void;
}) {
  const [overview, setOverview] = useState<ComponentBindingOverview | null>(null);
  const [replaceLoading, setReplaceLoading] = useState(false);
  // 预览计划一律来自后端（§13.3）：前端不构造、不重算，只展示并拿 token 去执行。
  const [replacePlan, setReplacePlan] = useState<ResolutionPlanPreview | null>(null);
  const replaceRequest = useRef(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  // 已处理的行占绝大多数（本例 257 中有 213），默认只看待处理。
  // 四态互不重叠：把"已绑定"与"已标记缺失"分开，后者常需单独核对是否真的台账没有。
  const [filter, setFilter] = useState<BindingFilter>("pending");
  // 正在批量替换的分组名；null 表示对话框未打开。
  const [replaceGroup, setReplaceGroup] = useState<string | null>(null);
  // 批量应用被后端整批拒绝时的提示，显示在对话框内而非页面上——用户正对着预览表。
  const [replaceError, setReplaceError] = useState<string | null>(null);
  const [splitSelection, setSplitSelection] = useState<Map<string, SplitTarget>>(new Map());
  const [splitDialogTargets, setSplitDialogTargets] = useState<SplitTarget[] | null>(null);
  const [splitPreview, setSplitPreview] = useState<ResolutionPlanPreview | null>(null);
  const [splitPreviewLoading, setSplitPreviewLoading] = useState(false);
  const [splitError, setSplitError] = useState<string | null>(null);
  const splitPreviewRequest = useRef(0);
  const [ratingTrees, setRatingTrees] = useState<RatingTreeVersionSummary[]>([]);
  const [selectedRatingTreeId, setSelectedRatingTreeId] = useState("");
  const [ratingTreeMessage, setRatingTreeMessage] = useState<string | null>(null);
  const [ratingTreeError, setRatingTreeError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    // 首屏只等概览。此前还并排拉一份完整台账（约 3.4 MB），两个都回来才渲染。
    fetchResolutionWorkspace(backendBaseUrl, importId)
      .then((workspace) => {
        if (cancelled) return;
        const boundOverview = toBindingOverview(workspace);
        setOverview(boundOverview);
        setError(null);
        onOverviewChange?.(boundOverview);
      })
      .catch((caught) => {
        if (!cancelled) setError(errorMessage(caught));
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    // 评定树版本列表是本页的辅助选择数据，不能阻塞构件行首屏显示。
    fetchRatingTreeVersions(backendBaseUrl)
      .then((versions) => {
        if (!cancelled) {
          setRatingTrees(versions);
          setRatingTreeError(null);
        }
      })
      .catch((caught) => {
        if (!cancelled) setRatingTreeError(errorMessage(caught));
      });
    return () => {
      cancelled = true;
    };
  }, [importId, bridgeId]);

  useEffect(() => {
    if (overview?.rating_tree?.version_id) {
      setSelectedRatingTreeId(overview.rating_tree.version_id);
    } else if (ratingTrees.length === 1) {
      setSelectedRatingTreeId(ratingTrees[0].id);
    }
  }, [overview?.rating_tree?.version_id, ratingTrees]);

  const progress = useMemo(
    () => (overview ? bindingProgress(overview) : { total: 0, settled: 0, pending: 0 }),
    [overview]
  );

  // 各状态计数，供筛选按钮显示。
  const counts = useMemo(() => {
    let pending = 0, bound = 0, missing = 0;
    for (const group of overview?.groups ?? []) {
      for (const row of group.rows) {
        if (row.status === "bound") bound += 1;
        else if (row.status === "missing") missing += 1;
        else pending += 1;
      }
    }
    return { pending, bound, missing, total: pending + bound + missing };
  }, [overview]);

  useEffect(() => {
    splitPreviewRequest.current += 1;
    setSplitDialogTargets(null);
    setSplitPreview(null);
    setSplitPreviewLoading(false);
    setSplitError(null);
  }, [importId]);

  async function loadSplitPreview(targets: SplitTarget[]) {
    const requestId = ++splitPreviewRequest.current;
    setSplitPreview(null);
    setSplitError(null);
    setSplitPreviewLoading(true);
    try {
      const plan = await createResolutionPlan(
        backendBaseUrl, importId,
        {
          operation_type: "range_expand",
          group_ids: targets.map((target) => target.group_id),
          expected_inventory_revision_id: requireRevisionId(overview!),
        },
        requireLockToken());
      if (splitPreviewRequest.current === requestId) setSplitPreview(plan);
    } catch (caught) {
      if (splitPreviewRequest.current === requestId) setSplitError(errorMessage(caught));
    } finally {
      if (splitPreviewRequest.current === requestId) setSplitPreviewLoading(false);
    }
  }

  function closeSplitDialog() {
    splitPreviewRequest.current += 1;
    setSplitDialogTargets(null);
    setSplitPreview(null);
    setSplitPreviewLoading(false);
    setSplitError(null);
  }

  // 只有未匹配/歧义行才可能可拆分，一条都没有时拆分按钮永远点不动，索性不占位。
  const splitEligibleCount = useMemo(
    () =>
      (overview?.groups ?? []).reduce(
        (sum, group) => sum + group.rows.filter((row) => row.split_eligible).length,
        0
      ),
    [overview]
  );

  // 已勾选目标的累计规模。2000 条那个上限是整批算的，跨分组勾选时更需要一个总数：
  // 没有它，用户只能在预览失败时才发现越界，而那时连预览都出不来。
  const splitProjection = useMemo(() => {
    let defects = 0;
    for (const group of overview?.groups ?? []) {
      for (const row of group.rows) {
        if (!row.split_eligible) continue;
        if (!splitSelection.has(splitTargetKey(group.part_name, row.component_number))) continue;
        defects += row.defect_count * (row.split_expanded_count ?? 0);
      }
    }
    return defects;
  }, [overview, splitSelection]);

  // 筛选后为空的分组不占位——否则整屏都是空标题。
  const visibleGroups = useMemo(() => {
    const groups = overview?.groups ?? [];
    if (filter === "all") return groups;
    const keep = (row: BindingRow) =>
      filter === "pending"
        ? row.status !== "bound" && row.status !== "missing"
        : filter === "bound"
          ? row.status === "bound"
          : row.status === "missing";
    return groups
      .map((group) => ({ ...group, rows: group.rows.filter(keep) }))
      .filter((group) => group.rows.length > 0);
  }, [overview, filter]);

  useEffect(() => {
    const eligible = new Set<string>();
    for (const group of overview?.groups ?? []) {
      for (const row of group.rows) {
        if (row.split_eligible) eligible.add(splitTargetKey(group.part_name, row.component_number));
      }
    }
    setSplitSelection((current) => {
      const next = new Map([...current].filter(([key]) => eligible.has(key)));
      return next.size === current.size ? current : next;
    });
  }, [overview]);

  // 单条绑定 / 标记缺失 / 取消绑定共用；三者都会改写后端草稿里的病害构件关联。

  // 台账版本变了：重新拉概览并说清楚发生了什么。只显示一条错误的话，用户会对着
  // 一份已经过期的候选反复重试。
  async function refreshAfterRevisionChange() {
    try {
      const next = toBindingOverview(
        await fetchResolutionWorkspace(backendBaseUrl, importId));
      setOverview(next);
      onOverviewChange?.(next);
      setSplitPreview(null);
      splitPreviewRequest.current += 1;
      setError("构件台账版本已变化，已为你刷新，请确认后重试。");
    } catch (caught) {
      setError(errorMessage(caught));
    }
  }

  // 写操作前取令牌；没有编辑权时抛出，由各自的 catch 转成行内提示。
  function requireLockToken(): string {
    if (lockToken === null) throw new Error("当前页面没有编辑权，无法修改绑定。");
    return lockToken;
  }

  // 六个绑定写接口现在都要求编辑锁，没有编辑权时点什么都会被后端拒。与其让用户
  // 勾满一屏、点下去才收到报错，不如直接禁用——概览本身是只读的，照常可看。
  // 页脚那两个"进入校对/稍后再绑"是导航，不受影响，仍只看 busy。
  const canEdit = lockToken !== null;
  const writeDisabled = busy || !canEdit;

  /**
   * 一条行的解析动作。三个动作同一个接口，差别只在 action 与目标集合。
   *
   * expected_version 取行上的组版本：两个页面同时改一条时，后到的那个必须撞版本
   * 冲突，而不是默默覆盖先到的。
   */
  async function resolveGroup(
    row: BindingRow,
    action: "bind" | "mark_missing" | "clear",
    componentIds: string[] = [],
  ) {
    return applyComponentResolution(
      backendBaseUrl,
      importId,
      row.group_id,
      {
        expected_version: row.version,
        action,
        // 两侧整体绑定靠 target_role 区分左右；单目标时统一是 primary。
        ...(action === "bind"
          ? {
              targets: componentIds.map((id, index) => ({
                bridge_component_id: id,
                target_role: componentIds.length > 1
                  ? (index === 0 ? "left" : "right")
                  : "primary",
              })),
            }
          : {}),
        expected_inventory_revision_id: requireRevisionId(overview!),
      },
      requireLockToken());
  }

  // 写操作只回受影响的组与最新统计（§13.2），所以成功后重取一次工作区。
  // 几百个组一次请求，比在前端合并局部结果再自己算一遍计数可靠。
  async function run(action: () => Promise<unknown>) {
    setBusy(true);
    try {
      await action();
      const next = toBindingOverview(
        await fetchResolutionWorkspace(backendBaseUrl, importId));
      setOverview(next);
      setError(null);
      onOverviewChange?.(next);
      onDraftInvalidated?.();
    } catch (caught) {
      if (isRevisionChanged(caught)) await refreshAfterRevisionChange();
      else setError(errorMessage(caught));
    } finally {
      setBusy(false);
    }
  }

  async function handleBindRatingTree() {
    if (!selectedRatingTreeId ||
        selectedRatingTreeId === overview?.rating_tree?.version_id) {
      return;
    }
    if (
      overview?.rating_tree &&
      !window.confirm(
        "切换评定树会同步切换年度规范组合，并在需要时派生一版兼容台账；当前草稿中的规范病害将重新匹配。确定继续吗？"
      )
    ) {
      return;
    }
    setBusy(true);
    setRatingTreeError(null);
    setRatingTreeMessage(null);
    try {
      const next = await bindInspectionRatingTree(
        backendBaseUrl,
        importId,
        selectedRatingTreeId,
        requireRevisionId(overview!),
        requireLockToken()
      );
      void next;
      const refreshed = toBindingOverview(
        await fetchResolutionWorkspace(backendBaseUrl, importId));
      setOverview(refreshed);
      onOverviewChange?.(refreshed);
      setRatingTreeMessage(
        overview?.rating_tree ? "评定树已切换。" : "评定树已绑定。"
      );
      onRatingTreeChange?.();
    } catch (caught) {
      setRatingTreeError(errorMessage(caught));
    } finally {
      setBusy(false);
    }
  }

  if (loading) return <p>正在加载构件绑定…</p>;
  if (error && !overview) return <p className="error-text" role="alert">{error}</p>;
  if (!overview) return <p>没有可绑定的病害。</p>;
  const allResolved = progress.total > 0 && progress.settled === progress.total;

  return (
    <section className="component-binding-workspace" aria-labelledby="component-binding-title">
      <div className="binding-heading">
        <h3 id="component-binding-title">构件绑定</h3>
        <div className="binding-heading-tools">
          {/* 拆分是动作而非筛选，故留在筛选组外，靠竖线隔开，免得看成第五个页签。 */}
          {splitEligibleCount > 0 ? (
            <button
              type="button"
              className="binding-split-selected"
              disabled={writeDisabled || splitSelection.size === 0}
              title={splitSelection.size === 0 ? "先勾选待拆分的构件行" : undefined}
              onClick={() => {
                const targets = [...splitSelection.values()];
                setSplitDialogTargets(targets);
                void loadSplitPreview(targets);
              }}
            >
              拆分构件
              {splitSelection.size > 0 ? (
                <span className="binding-split-count">{splitSelection.size}</span>
              ) : null}
              {splitProjection > 0 ? (
                <span className="binding-split-projection">约 {splitProjection} 条</span>
              ) : null}
            </button>
          ) : null}
          <div className="binding-filters" role="group" aria-label="按状态筛选">
            {([
              ["pending", "待处理", counts.pending],
              ["bound", "已绑定", counts.bound],
              ["missing", "已标记缺失", counts.missing],
              ["all", "全部", counts.total],
            ] as const).map(([key, label, count]) => (
              <button
                key={key}
                type="button"
                className={filter === key ? "binding-filter active" : "binding-filter"}
                aria-pressed={filter === key}
                onClick={() => {
                  setFilter(key);
                  if (key !== "pending" && key !== "all") setSplitSelection(new Map());
                }}
              >
                {label} {count}
              </button>
            ))}
          </div>
        </div>
      </div>
      <div className="binding-rating-tree" aria-label="年度评定树绑定">
        <div className="binding-rating-tree-current">
          <span>年度评定树</span>
          <strong>
            {overview.rating_tree
              ? `${overview.rating_tree.tree_name} ${overview.rating_tree.package_version}`
              : "尚未绑定"}
          </strong>
          {overview.rating_tree ? (
            <small>
              H21 {overview.rating_tree.h21_package_version}
              {" · "}
              JTG 5120 {overview.rating_tree.maintenance_package_version}
            </small>
          ) : (
            <small>绑定后，病害匹配与系统评定将统一使用该版本。</small>
          )}
        </div>
        <label>
          <span>选择已发布版本</span>
          <select
            aria-label="选择年度评定树"
            value={selectedRatingTreeId}
            disabled={writeDisabled || ratingTrees.length === 0}
            onChange={(event) => {
              setSelectedRatingTreeId(event.target.value);
              setRatingTreeMessage(null);
              setRatingTreeError(null);
            }}
          >
            <option value="">
              {ratingTrees.length === 0 ? "暂无可用评定树" : "请选择评定树"}
            </option>
            {ratingTrees.map((tree) => (
              <option key={tree.id} value={tree.id}>
                {tree.tree_name} {tree.package_version}
                {tree.h21_package_version
                  ? ` · H21 ${tree.h21_package_version}`
                  : ""}
              </option>
            ))}
          </select>
        </label>
        <button
          type="button"
          className="binding-rating-tree-action"
          disabled={
            writeDisabled ||
            !selectedRatingTreeId ||
            selectedRatingTreeId === overview.rating_tree?.version_id
          }
          onClick={() => void handleBindRatingTree()}
        >
          {overview.rating_tree ? "切换评定树" : "绑定评定树"}
        </button>
      </div>
      {ratingTreeMessage ? (
        <p className="binding-rating-tree-success" role="status">
          {ratingTreeMessage}
        </p>
      ) : null}
      {ratingTreeError ? (
        <p className="error-text" role="alert">{ratingTreeError}</p>
      ) : null}
      {error ? <p className="error-text" role="alert">{error}</p> : null}
      {/* 控件已经按 canEdit 全部禁用了，但灰掉不解释等于让人猜。 */}
      {!canEdit ? (
        <p className="warning-text">
          当前页面没有编辑权，绑定与拆分均不可用；取得编辑权后即可操作。
        </p>
      ) : null}
      {!overview.inventory_confirmed ? (
        <p className="error-text" role="alert">
          该桥构件台账尚未确认，请先建立并确认台账后再进行构件绑定。
        </p>
      ) : null}
      {overview.inventory_confirmed ? (
        <>
      {overview.groups.length === 0 ? <p>本次导入没有需要绑定的病害。</p> : null}
      {overview.groups.length > 0 && visibleGroups.length === 0 ? (
        // "全部处理完毕"只在待处理筛选下成立；其余筛选为空只是该状态没有行。
        <p className={filter === "pending" ? "binding-all-done" : "empty-hint"}>
          {filter === "pending" ? "全部构件已处理完毕。" : "该状态下没有构件。"}
        </p>
      ) : null}
      {visibleGroups.map((group) => (
        <div className="binding-group" key={group.part_name}>
          <div className="binding-group-heading">
            <strong>{group.part_name}</strong>
            <span className="binding-group-counts">
              共 {group.total}
              {group.unmatched > 0 ? ` · 未匹配 ${group.unmatched}` : ""}
              {group.ambiguous > 0 ? ` · 歧义 ${group.ambiguous}` : ""}
              {group.missing > 0 ? ` · 缺失 ${group.missing}` : ""}
            </span>
            <div className="binding-group-actions">
              {group.rows.some((row) => row.split_eligible) ? (
                <GroupSplitSelector
                  partName={group.part_name}
                  eligibleRows={group.rows.filter((row) => row.split_eligible)}
                  selection={splitSelection}
                  busy={writeDisabled}
                  onChange={(checked) => {
                    const eligibleRows = group.rows.filter((row) => row.split_eligible);
                    setSplitSelection((current) => {
                      const next = new Map(current);
                      for (const row of eligibleRows) {
                        const key = splitTargetKey(group.part_name, row.component_number);
                        if (checked) {
                          next.set(key, {
                            group_id: row.group_id,
                            part_name: group.part_name,
                            component_number: row.component_number,
                          });
                        } else {
                          next.delete(key);
                        }
                      }
                      return next;
                    });
                  }}
                />
              ) : null}
              {/* 写法差异按部件成规律，故批量替换逐组进行；无待处理行时无从替换。 */}
              {group.unmatched + group.ambiguous > 0 ? (
                <button
                  type="button"
                  className="binding-bulk-replace"
                  disabled={writeDisabled}
                  onClick={() => { setReplaceError(null); setReplaceGroup(group.part_name); }}
                >
                  批量替换
                </button>
              ) : null}
            </div>
          </div>
          {group.rows.map((row) => (
            <div className="binding-row" key={row.component_number}>
              {row.split_eligible ? (
                <input
                  type="checkbox"
                  className="binding-row-split-checkbox"
                  aria-label={`选择拆分 ${row.component_number}`}
                  checked={splitSelection.has(splitTargetKey(group.part_name, row.component_number))}
                  disabled={writeDisabled}
                  onChange={(event) => {
                    const key = splitTargetKey(group.part_name, row.component_number);
                    setSplitSelection((current) => {
                      const next = new Map(current);
                      if (event.target.checked) {
                        next.set(key, {
                          group_id: row.group_id,
                          part_name: group.part_name,
                          component_number: row.component_number,
                        });
                      } else next.delete(key);
                      return next;
                    });
                  }}
                />
              ) : <span className="binding-row-split-placeholder" aria-hidden="true" />}
              <span className="binding-row-number">{row.component_number}</span>
              <span className="binding-row-refs">引用 {row.defect_count} 条</span>
              <span className={`binding-status binding-status-${row.status}`}>
                {STATUS_LABELS[row.status] ?? row.status}
              </span>
              <RowAction
                row={row}
                revisionId={requireRevisionId(overview)}
                busy={writeDisabled}
                onBind={(id) => run(() => resolveGroup(row, "bind", [id]))}
                onBindMulti={(ids) => run(() => resolveGroup(row, "bind", ids))}
                onMarkMissing={() => run(() => resolveGroup(row, "mark_missing"))}
                onClear={() => run(() => resolveGroup(row, "clear"))}
              />
            </div>
          ))}
        </div>
      ))}
      {replaceGroup !== null ? (
        <BulkReplaceDialog
          partName={replaceGroup}
          plan={replacePlan}
          previewing={replaceLoading}
          busy={busy}
          error={replaceError}
          onClose={() => {
            setReplacePlan(null);
            setReplaceError(null);
            setReplaceGroup(null);
          }}
          onPreview={async (find, replace) => {
            // 预览由后端生成：前端不再拿一份台账自己算一遍。
            setReplaceLoading(true);
            try {
              const plan = await createResolutionPlan(
                backendBaseUrl, importId,
                {
                  operation_type: "bulk_replace",
                  source_component_name: replaceGroup,
                  find,
                  replace,
                  expected_inventory_revision_id: requireRevisionId(overview),
                },
                requireLockToken());
              setReplacePlan(plan);
              setReplaceError(null);
            } catch (caught) {
              setReplacePlan(null);
              setReplaceError(errorMessage(caught));
            } finally {
              setReplaceLoading(false);
            }
          }}
          onApply={async (planToken) => {
            setBusy(true);
            try {
              // 只提交 plan token：“用户看到的计划”与“实际执行的计划”因此天然是同一份。
              await applyResolutionPlan(
                backendBaseUrl, importId, planToken, requireLockToken());
              const next = toBindingOverview(
                await fetchResolutionWorkspace(backendBaseUrl, importId));
              setOverview(next);
              setError(null);
              onOverviewChange?.(next);
              setReplacePlan(null);
              setReplaceGroup(null);
            } catch (caught) {
              // 整批被拒时留在对话框里显示原因，用户可改模式重来。
              setReplaceError(errorMessage(caught));
            } finally {
              setBusy(false);
            }
          }}
        />
      ) : null}
      {splitDialogTargets ? (
        <ComponentRangeSplitDialog
          preview={splitPreview}
          loading={splitPreviewLoading}
          busy={busy}
          error={splitError}
          onClose={closeSplitDialog}
          onRetry={() => void loadSplitPreview(splitDialogTargets)}
          onApply={async (planToken) => {
            setBusy(true);
            setSplitError(null);
            try {
              await applyResolutionPlan(
                backendBaseUrl, importId, planToken, requireLockToken());
              const applied = toBindingOverview(
                await fetchResolutionWorkspace(backendBaseUrl, importId));
              setOverview(applied);
              onOverviewChange?.(applied);
              // 拆分会增删病害并搬动照片归属，父页面的草稿必须重取。
              onDraftInvalidated?.();
              setSplitSelection(new Map());
              closeSplitDialog();
              setError(null);
            } catch (caught) {
              setSplitError(errorMessage(caught));
            } finally {
              setBusy(false);
            }
          }}
        />
      ) : null}
      {/* 这两个按钮都是"离开绑定、去校对"。作为校对页的一个分区嵌入时不传回调，
          此时不渲染页脚，否则会留下两个点了没反应的死按钮。 */}
      {onEnterReview ? (
        <div className="binding-footer">
          <button
            type="button"
            className="binding-enter-review"
            disabled={busy || !allResolved}
            onClick={() => onEnterReview()}
          >
            {allResolved ? "全部绑定完成，进入校对" : "仍有未处理构件"}
          </button>
          <button type="button" className="binding-later" disabled={busy} onClick={() => onEnterReview()}>
            稍后再绑
          </button>
        </div>
      ) : null}
        </>
      ) : null}
    </section>
  );
}
