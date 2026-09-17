import { useEffect, useMemo, useRef, useState } from "react";
import { Alert, Button, Card, Checkbox, Empty, Flex, Input, Pagination, Progress, Result, Select, Table, Tag, Typography } from "antd";
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
import { bindInspectionRatingTree } from "../../api/inspectionRatingTreeApi";

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

const MAX_SEARCH_RESULTS = 20;
const DEFAULT_PAGE_SIZE = 20;

const STATUS_LABELS: Record<string, string> = {
  bound: "已绑定",
  unmatched: "未匹配",
  ambiguous: "歧义",
  missing: "已标记缺失",
};

/* 状态色：已绑定绿、未匹配琥珀、歧义蓝、缺失灰。 */
const STATUS_TAG_COLORS: Record<string, string> = {
  bound: "success",
  unmatched: "warning",
  ambiguous: "processing",
  missing: "default",
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

  return (
    <Checkbox
      aria-label={`全选 ${partName} 待拆分构件`}
      checked={allSelected}
      indeterminate={selectedCount > 0 && !allSelected}
      disabled={busy}
      onChange={(event) => onChange(event.target.checked)}
    >
      {allSelected ? "取消全选" : "全选待拆分"} {eligibleRows.length}
      {projectedDefects > 0 ? (
        <Typography.Text type="secondary"> 约 {projectedDefects} 条</Typography.Text>
      ) : null}
    </Checkbox>
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
      <Flex align="center" gap={8} wrap>
        {rebound ? (
          <Typography.Text type="secondary">
            → {bound.component_number} / {bound.site_component_type}
          </Typography.Text>
        ) : null}
        {!bound ? <Typography.Text type="secondary">已绑定构件</Typography.Text> : null}
        <Button size="small" disabled={busy} aria-label={`取消绑定 ${row.component_number}`} onClick={onClear}>
          取消绑定
        </Button>
      </Flex>
    );
  }
  if (row.status === "missing") {
    return (
      <Flex align="center" gap={8} wrap>
        <Typography.Text type="secondary">台账确无此构件</Typography.Text>
        <Button size="small" disabled={busy} aria-label={`取消标记 ${row.component_number}`} onClick={onClear}>
          取消标记
        </Button>
      </Flex>
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
    <Flex align="center" gap={8} wrap>
      <Select
        aria-label={`为 ${row.component_number} 选择实际构件`}
        style={{ minWidth: 260 }}
        value=""
        disabled={busy}
        onChange={(value: string) => {
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
        options={[
          {
            value: "",
            label: term && options.size === 0 ? "没有匹配的构件" : "请选择实际构件（可先搜索）",
          },
          /* "两侧"排在候选之上：报告写"两侧护栏"时，逐个绑左右两件才是对的做法，
             单选任一侧都会让另一侧留在满分。 */
          ...(row.side_pair_option
            ? [{ value: SIDE_PAIR_VALUE, label: row.side_pair_option.label }]
            : []),
          ...[...options.values()].map((option) => ({
            value: option.bridgeComponentId,
            label: `${option.candidate ? "候选 · " : ""}${option.componentNumber} / ${option.siteComponentType} / ${option.siteName}`,
          })),
        ]}
      />
      <Input
        aria-label={`搜索实际构件 ${row.component_number}`}
        placeholder="编号、类别或现场名"
        style={{ width: 180 }}
        value={search}
        disabled={busy}
        onChange={(event) => setSearch(event.target.value)}
      />
      {/* 搜索失败只报在行内，概览带来的候选项照常可用。 */}
      {searchError ? <Typography.Text type="danger">{searchError}</Typography.Text> : null}
      <Button size="small" disabled={busy} aria-label={`标记缺失 ${row.component_number}`} onClick={onMarkMissing}>
        标记缺失
      </Button>
    </Flex>
  );
}

export function ComponentBindingWorkspace({
  importId,
  bridgeId,
  importStatus,
  lockToken,
  onEnterReview,
  onOverviewChange,
  onRatingTreeChange,
  onDraftInvalidated,
}: {
  importId: string;
  bridgeId: string;
  /**
   * 导入记录状态。只有「待校对」的记录才有构件绑定工作区——后端对其余状态一律拒绝。
   * 传进来是为了让本组件自己把这件事说清楚，而不是发一次注定失败的请求、再把后端的
   * 拒绝原文当报错打在页面上。
   */
  importStatus?: string;
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
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(DEFAULT_PAGE_SIZE);
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

  // 只有「待校对」的记录有绑定工作区；其余状态不发这一趟，省掉一次注定 4xx 的请求。
  const bindingAvailable = importStatus === undefined || importStatus === "待校对";

  useEffect(() => {
    if (!bindingAvailable) {
      setLoading(false);
      return;
    }
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
  }, [importId, bridgeId, bindingAvailable]);

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

  const visibleRowTotal = useMemo(
    () => visibleGroups.reduce((sum, group) => sum + group.rows.length, 0),
    [visibleGroups]
  );

  // 与构件台账一致：跨分组按行分页，每页只挂载当前页数据。一个分组跨页时，
  // 当前页仍保留分组标题，避免用户失去部件类别上下文。
  const pagedGroups = useMemo(() => {
    const start = (page - 1) * pageSize;
    const end = start + pageSize;
    let cursor = 0;
    return visibleGroups.flatMap((group) => {
      const groupStart = cursor;
      const groupEnd = cursor + group.rows.length;
      cursor = groupEnd;
      if (groupEnd <= start || groupStart >= end) return [];
      const rows = group.rows.slice(
        Math.max(0, start - groupStart),
        Math.min(group.rows.length, end - groupStart)
      );
      return [{ ...group, rows }];
    });
  }, [visibleGroups, page, pageSize]);

  useEffect(() => {
    const lastPage = Math.max(1, Math.ceil(visibleRowTotal / pageSize));
    if (page > lastPage) setPage(lastPage);
  }, [page, pageSize, visibleRowTotal]);

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

  /* 已入库 / 已作废的记录没有绑定工作区，这不是错误，是这一步已经过去了。
     原来在这里打一行后端拒绝原文的红字，既像出了故障，也没说清还能去哪儿看。 */
  if (!bindingAvailable) {
    return (
      <Card>
        <Result
          status="success"
          title="构件绑定已完成"
          subTitle={`这条导入记录状态为「${importStatus}」，构件绑定只在校对阶段开放。绑定结果仍可在「病害与照片」里逐条查看，评定结论见「系统技术状况评定」。`}
        />
      </Card>
    );
  }

  if (loading) return <Card loading />;
  if (error && !overview) return <Alert type="error" showIcon role="alert" title={error} />;
  if (!overview) return <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="没有可绑定的病害。" />;
  const allResolved = progress.total > 0 && progress.settled === progress.total;
  const progressPercent = progress.total > 0
    ? Math.round((progress.settled / progress.total) * 100)
    : 0;

  function openSelectedSplitPreview() {
    const targets = [...splitSelection.values()];
    setSplitDialogTargets(targets);
    void loadSplitPreview(targets);
  }

  return (
    <Flex vertical gap={14}>
      <Card size="small" aria-label="年度评定树绑定">
        <Flex align="flex-end" justify="space-between" gap={16} wrap>
          <Flex vertical gap={2}>
            <Typography.Text type="secondary">年度评定树</Typography.Text>
            <Typography.Text strong>
              {overview.rating_tree
                ? `${overview.rating_tree.tree_name} ${overview.rating_tree.package_version}`
                : "尚未绑定"}
            </Typography.Text>
            <Typography.Text type="secondary">
              {overview.rating_tree
                ? `H21 ${overview.rating_tree.h21_package_version} · JTG 5120 ${overview.rating_tree.maintenance_package_version}`
                : "绑定后，病害匹配与系统评定将统一使用该版本。"}
            </Typography.Text>
          </Flex>
          <Flex align="flex-end" gap={8} wrap>
            <Flex vertical gap={4}>
              <Typography.Text type="secondary">选择已发布版本</Typography.Text>
              <Select
                aria-label="选择年度评定树"
                style={{ minWidth: 280 }}
                value={selectedRatingTreeId}
                disabled={writeDisabled || ratingTrees.length === 0}
                onChange={(value: string) => {
                  setSelectedRatingTreeId(value);
                  setRatingTreeMessage(null);
                  setRatingTreeError(null);
                }}
                options={[
                  { value: "", label: ratingTrees.length === 0 ? "暂无可用评定树" : "请选择评定树" },
                  ...ratingTrees.map((tree) => ({
                    value: tree.id,
                    label: `${tree.tree_name} ${tree.package_version}${tree.h21_package_version ? ` · H21 ${tree.h21_package_version}` : ""}`,
                  })),
                ]}
              />
            </Flex>
            <Button
              type="primary"
              disabled={
                writeDisabled ||
                !selectedRatingTreeId ||
                selectedRatingTreeId === overview.rating_tree?.version_id
              }
              onClick={() => void handleBindRatingTree()}
            >
              {overview.rating_tree ? "切换评定树" : "绑定评定树"}
            </Button>
          </Flex>
        </Flex>
      </Card>
      {ratingTreeMessage ? <Alert type="success" showIcon role="status" title={ratingTreeMessage} /> : null}
      {ratingTreeError ? <Alert type="error" showIcon role="alert" title={ratingTreeError} /> : null}
      <Card styles={{ body: { display: "flex", flexDirection: "column", gap: 14 } }}>
        <Flex align="flex-start" justify="space-between" gap={16} wrap>
          <Flex vertical gap={8} style={{ minWidth: 260 }}>
            <Flex align="center" gap={10} wrap>
              <Typography.Title level={5} id="component-binding-title" style={{ margin: 0 }}>构件绑定</Typography.Title>
              <Tag color={canEdit ? "processing" : "default"} variant="filled">{canEdit ? "编辑中" : "只读"}</Tag>
              <Typography.Text type="secondary">
                台账版本 · {overview.inventory_confirmed ? "已确认" : "未确认"}
              </Typography.Text>
            </Flex>
            <Flex align="center" gap={10} aria-label={`已处理 ${progress.settled} / ${progress.total}`}>
              <Typography.Text type="secondary">已处理 {progress.settled} / {progress.total}</Typography.Text>
              <Progress
                percent={progressPercent}
                showInfo={false}
                size={{ height: 8 }}
                style={{ flex: 1, minWidth: 120, margin: 0 }}
              />
              <Typography.Text strong>{progressPercent}%</Typography.Text>
            </Flex>
          </Flex>
          <Flex align="center" gap={10} wrap>
            <Flex gap={6} role="group" aria-label="按状态筛选" wrap>
              {([
                ["pending", "待处理", counts.pending],
                ["bound", "已绑定", counts.bound],
                ["missing", "已标记缺失", counts.missing],
                ["all", "全部", counts.total],
              ] as const).map(([key, label, count]) => (
                <Button
                  key={key}
                  type={filter === key ? "primary" : "default"}
                  aria-label={`${label} ${count}`}
                  aria-pressed={filter === key}
                  onClick={() => {
                    setFilter(key);
                    setPage(1);
                    if (key !== "pending" && key !== "all") setSplitSelection(new Map());
                  }}
                >
                  {label} {count}
                </Button>
              ))}
            </Flex>
            {splitEligibleCount > 0 ? (
              <Button
                disabled={writeDisabled || splitSelection.size === 0}
                title={splitSelection.size === 0 ? "先勾选待拆分的构件行" : undefined}
                onClick={openSelectedSplitPreview}
              >
                拆分构件
                {splitSelection.size > 0 ? ` ${splitSelection.size}` : ""}
                {splitProjection > 0 ? ` · 约 ${splitProjection} 条` : ""}
              </Button>
            ) : null}
          </Flex>
        </Flex>
        {splitEligibleCount > 0 ? (
          <Typography.Text type="secondary">
            区间拆分会增加解析实例并重新计算评分；照片需要在拆分后人工核对归属。
          </Typography.Text>
        ) : null}
      {error ? <Alert type="error" showIcon role="alert" title={error} /> : null}
      {/* 控件已经按 canEdit 全部禁用了，但灰掉不解释等于让人猜。 */}
      {!canEdit ? (
        <Alert type="warning" showIcon role="note" title="当前页面没有编辑权，绑定与拆分均不可用；取得编辑权后即可操作。" />
      ) : null}
      {!overview.inventory_confirmed ? (
        <Alert type="error" showIcon role="alert" title="该桥构件台账尚未确认，请先建立并确认台账后再进行构件绑定。" />
      ) : null}
      {overview.inventory_confirmed ? (
        <>
      {overview.groups.length === 0 ? (
        <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="本次导入没有需要绑定的病害。" />
      ) : null}
      {overview.groups.length > 0 && visibleGroups.length === 0 ? (
        // "全部处理完毕"只在待处理筛选下成立；其余筛选为空只是该状态没有行。
        filter === "pending" ? (
          <Alert
            type="success"
            showIcon
            role="status"
            title="全部构件已处理完毕。"
            description={`${counts.bound} 个已绑定，${counts.missing} 个已标记缺失，可进入下一分区继续校对。`}
            action={
              <Button size="small" onClick={() => { setFilter("all"); setPage(1); }}>
                查看全部 {counts.total}
              </Button>
            }
          />
        ) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="该状态下没有构件。" />
      ) : null}
      {pagedGroups.map((group) => (
        <Card
          size="small"
          key={group.part_name}
          title={
            <Flex align="center" gap={10} wrap>
              <Typography.Text strong>{group.part_name}</Typography.Text>
              <Typography.Text type="secondary" style={{ fontWeight: "normal" }}>
                共 {group.total}
                {group.unmatched > 0 ? ` · 未匹配 ${group.unmatched}` : ""}
                {group.ambiguous > 0 ? ` · 歧义 ${group.ambiguous}` : ""}
                {group.missing > 0 ? ` · 缺失 ${group.missing}` : ""}
              </Typography.Text>
            </Flex>
          }
          extra={
            <Flex align="center" gap={10} wrap>
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
                <Button
                  size="small"
                  disabled={writeDisabled}
                  onClick={() => { setReplaceError(null); setReplaceGroup(group.part_name); }}
                >
                  批量替换
                </Button>
              ) : null}
            </Flex>
          }
        >
          <Table<BindingRow>
            rowKey="component_number"
            size="small"
            pagination={false}
            scroll={{ x: 900 }}
            dataSource={group.rows}
            columns={[
              {
                title: "",
                key: "split",
                width: 46,
                align: "center",
                render: (_value: unknown, row: BindingRow) => (row.split_eligible ? (
                  <Checkbox
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
                ) : null),
              },
              {
                title: "报告构件",
                key: "number",
                width: 190,
                render: (_value: unknown, row: BindingRow) => (
                  <Flex vertical>
                    <Typography.Text strong>{row.component_number}</Typography.Text>
                    {row.split_eligible && row.split_expanded_count ? (
                      <Typography.Text type="secondary">可展开到 {row.split_expanded_count} 件</Typography.Text>
                    ) : null}
                  </Flex>
                ),
              },
              {
                title: "引用病害",
                key: "refs",
                width: 110,
                render: (_value: unknown, row: BindingRow) => `引用 ${row.defect_count} 条`,
              },
              {
                title: "状态",
                key: "status",
                width: 110,
                render: (_value: unknown, row: BindingRow) => (
                  <Tag color={STATUS_TAG_COLORS[row.status] ?? "default"} variant="filled">
                    {STATUS_LABELS[row.status] ?? row.status}
                  </Tag>
                ),
              },
              {
                title: "候选与实际构件 / 操作",
                key: "action",
                render: (_value: unknown, row: BindingRow) => (
                  <RowAction
                    row={row}
                    revisionId={requireRevisionId(overview)}
                    busy={writeDisabled}
                    onBind={(id) => run(() => resolveGroup(row, "bind", [id]))}
                    onBindMulti={(ids) => run(() => resolveGroup(row, "bind", ids))}
                    onMarkMissing={() => run(() => resolveGroup(row, "mark_missing"))}
                    onClear={() => run(() => resolveGroup(row, "clear"))}
                  />
                ),
              },
            ]}
          />
        </Card>
      ))}
      {visibleRowTotal > 0 ? (
        <Flex justify="end">
          <Pagination
            align="end"
            current={page}
            pageSize={pageSize}
            total={visibleRowTotal}
            pageSizeOptions={[20, 50, 100]}
            showSizeChanger
            showQuickJumper
            showTotal={(total) => `共 ${total.toLocaleString()} 条`}
            disabled={busy}
            size="small"
            onChange={(nextPage, nextPageSize) => {
              setPage(nextPageSize === pageSize ? nextPage : 1);
              setPageSize(nextPageSize);
            }}
          />
        </Flex>
      ) : null}
      {splitSelection.size > 0 ? (
        <Alert
          type="info"
          showIcon
          role="status"
          title={`已选择 ${splitSelection.size} 个范围${splitProjection > 0 ? ` · 预计生成约 ${splitProjection} 条解析实例` : ""}`}
          action={
            <Button size="small" disabled={writeDisabled} onClick={openSelectedSplitPreview}>
              预览拆分影响
            </Button>
          }
        />
      ) : null}
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
          onClearPlan={() => {
            setReplacePlan(null);
            setReplaceError(null);
          }}
          onPreview={async (find, replace) => {
            // 预览由后端生成：前端不再拿一份台账自己算一遍。
            // 自动预览会连着发几次；只认最后一次的结果，否则先发后到的旧计划会盖掉新的。
            const seq = ++replaceRequest.current;
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
              if (seq !== replaceRequest.current) return;
              setReplacePlan(plan);
              setReplaceError(null);
            } catch (caught) {
              if (seq !== replaceRequest.current) return;
              setReplacePlan(null);
              setReplaceError(errorMessage(caught));
            } finally {
              if (seq === replaceRequest.current) setReplaceLoading(false);
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
        <Flex gap={8} wrap>
          <Button
            type="primary"
            disabled={busy || !allResolved}
            onClick={() => onEnterReview()}
          >
            {allResolved ? "全部绑定完成，进入校对" : "仍有未处理构件"}
          </Button>
          <Button disabled={busy} onClick={() => onEnterReview()}>稍后再绑</Button>
        </Flex>
      ) : null}
        </>
      ) : null}
      </Card>
    </Flex>
  );
}
