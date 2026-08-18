import { useEffect, useMemo, useRef, useState } from "react";

import {
  fetchLatestComponentInventory,
  type ComponentInventoryEntry,
  type ComponentInventoryRevision,
} from "../../api/componentInventoryApi";
import {
  bindComponent,
  bindComponentsBatch,
  bindInspectionRatingTree,
  bindingProgress,
  clearComponentBinding,
  fetchComponentBinding,
  markComponentMissing,
  previewComponentRangeSplit,
  applyComponentRangeSplit,
  INVENTORY_REVISION_CHANGED,
  type BindingRow,
  type BindingTarget,
  type ComponentRangeSplitPreview,
  type ComponentBindingOverview,
} from "../../api/importBindingApi";
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

function usableEntries(inventory: ComponentInventoryRevision | null): ComponentInventoryEntry[] {
  if (!inventory) return [];
  return inventory.entries.filter(
    (entry) => entry.is_active && entry.mappings.some((mapping) => mapping.is_active)
  );
}

function errorMessage(caught: unknown): string {
  if (caught instanceof ApiError) {
    if (caught.code === "component_binding_conflict") {
      return "台账未确认、导入不在待校对阶段，或所选构件与该部件类别不符。";
    }
    return caught.message;
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

interface RowActionProps {
  row: BindingRow;
  entries: ComponentInventoryEntry[];
  byId: Map<string, ComponentInventoryEntry>;
  busy: boolean;
  onBind: (bridgeComponentId: string) => void;
  onMarkMissing: () => void;
  onClear: () => void;
}

function RowAction({ row, entries, byId, busy, onBind, onMarkMissing, onClear }: RowActionProps) {
  const [search, setSearch] = useState("");

  if (row.status === "bound") {
    const bound = row.bridge_component_id ? byId.get(row.bridge_component_id) : undefined;
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

  const candidates = new Set(row.candidate_component_ids);
  const term = search.trim();
  const options = new Map<string, ComponentInventoryEntry>();
  for (const id of row.candidate_component_ids) {
    const entry = byId.get(id);
    if (entry) options.set(entry.bridge_component_id, entry);
  }
  if (term) {
    for (const entry of entries) {
      if (options.size >= MAX_SEARCH_RESULTS) break;
      if (
        entry.component_number.includes(term) ||
        entry.site_component_type.includes(term) ||
        entry.site_name.includes(term)
      ) {
        options.set(entry.bridge_component_id, entry);
      }
    }
  }

  return (
    <div className="binding-row-action">
      <input
        aria-label={`搜索实际构件 ${row.component_number}`}
        placeholder="输入编号或名称搜索"
        value={search}
        disabled={busy || entries.length === 0}
        onChange={(event) => setSearch(event.target.value)}
      />
      <select
        aria-label={`为 ${row.component_number} 选择实际构件`}
        value=""
        disabled={busy || entries.length === 0}
        onChange={(event) => {
          if (event.target.value) onBind(event.target.value);
        }}
      >
        <option value="">
          {term && options.size === 0 ? "没有匹配的构件" : "请选择实际构件（可先搜索）"}
        </option>
        {[...options.values()].map((entry) => (
          <option key={entry.id} value={entry.bridge_component_id}>
            {candidates.has(entry.bridge_component_id) ? "候选 · " : ""}
            {entry.component_number} / {entry.site_component_type}
          </option>
        ))}
      </select>
      <button type="button" disabled={busy} aria-label={`标记缺失 ${row.component_number}`} onClick={onMarkMissing}>
        标记缺失
      </button>
    </div>
  );
}

export function ComponentBindingWorkspace({
  importId,
  bridgeId,
  onEnterReview,
  onOverviewChange,
  onRatingTreeChange,
  onDraftInvalidated,
}: {
  importId: string;
  bridgeId: string;
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
  const [inventory, setInventory] = useState<ComponentInventoryRevision | null>(null);
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
  const [splitSelection, setSplitSelection] = useState<Map<string, BindingTarget>>(new Map());
  const [splitDialogTargets, setSplitDialogTargets] = useState<BindingTarget[] | null>(null);
  const [splitPreview, setSplitPreview] = useState<ComponentRangeSplitPreview | null>(null);
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
    Promise.all([
      fetchComponentBinding(backendBaseUrl, importId),
      fetchLatestComponentInventory(backendBaseUrl, bridgeId).catch(() => null),
    ])
      .then(([boundOverview, revision]) => {
        if (cancelled) return;
        setOverview(boundOverview);
        setInventory(revision);
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

  const entries = useMemo(() => usableEntries(inventory), [inventory]);
  const byId = useMemo(
    () => new Map(entries.map((entry) => [entry.bridge_component_id, entry])),
    [entries]
  );

  const progress = useMemo(
    () => (overview ? bindingProgress(overview) : { total: 0, resolved: 0, pending: 0 }),
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

  async function loadSplitPreview(targets: BindingTarget[]) {
    const requestId = ++splitPreviewRequest.current;
    setSplitPreview(null);
    setSplitError(null);
    setSplitPreviewLoading(true);
    try {
      const preview = await previewComponentRangeSplit(
        backendBaseUrl, importId, targets, requireRevisionId(overview!));
      if (splitPreviewRequest.current === requestId) setSplitPreview(preview);
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
        if (row.split_eligible) eligible.add(`${group.part_name}\n${row.component_number}`);
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
      const next = await fetchComponentBinding(backendBaseUrl, importId);
      setOverview(next);
      onOverviewChange?.(next);
      setSplitPreview(null);
      splitPreviewRequest.current += 1;
      setError("构件台账版本已变化，已为你刷新，请确认后重试。");
    } catch (caught) {
      setError(errorMessage(caught));
    }
  }

  async function run(action: () => Promise<ComponentBindingOverview>) {
    setBusy(true);
    try {
      const next = await action();
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
        requireRevisionId(overview!)
      );
      setOverview(next);
      onOverviewChange?.(next);
      setInventory(
        await fetchLatestComponentInventory(backendBaseUrl, bridgeId).catch(
          () => inventory
        )
      );
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
  const allResolved = progress.total > 0 && progress.resolved === progress.total;

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
              disabled={busy || splitSelection.size === 0}
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
            disabled={busy || ratingTrees.length === 0}
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
            busy ||
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
            {/* 写法差异按部件成规律，故批量替换逐组进行；无待处理行时无从替换。 */}
            {group.unmatched + group.ambiguous > 0 ? (
              <button
                type="button"
                className="binding-bulk-replace"
                disabled={busy}
                onClick={() => { setReplaceError(null); setReplaceGroup(group.part_name); }}
              >
                批量替换
              </button>
            ) : null}
          </div>
          {group.rows.map((row) => (
            <div className="binding-row" key={row.component_number}>
              {row.split_eligible ? (
                <input
                  type="checkbox"
                  className="binding-row-split-checkbox"
                  aria-label={`选择拆分 ${row.component_number}`}
                  checked={splitSelection.has(`${group.part_name}\n${row.component_number}`)}
                  disabled={busy}
                  onChange={(event) => {
                    const key = `${group.part_name}\n${row.component_number}`;
                    setSplitSelection((current) => {
                      const next = new Map(current);
                      if (event.target.checked) {
                        next.set(key, {
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
                entries={entries}
                byId={byId}
                busy={busy}
                onBind={(id) =>
                  run(() =>
                    bindComponent(backendBaseUrl, importId, {
                      part_name: group.part_name,
                      component_number: row.component_number,
                      bridge_component_id: id,
                    }, requireRevisionId(overview))
                  )
                }
                onMarkMissing={() =>
                  run(() =>
                    markComponentMissing(backendBaseUrl, importId, {
                      part_name: group.part_name,
                      component_number: row.component_number,
                    }, requireRevisionId(overview))
                  )
                }
                onClear={() =>
                  run(() =>
                    clearComponentBinding(backendBaseUrl, importId, {
                      part_name: group.part_name,
                      component_number: row.component_number,
                    }, requireRevisionId(overview))
                  )
                }
              />
            </div>
          ))}
        </div>
      ))}
      {replaceGroup !== null ? (
        <BulkReplaceDialog
          partName={replaceGroup}
          rows={overview.groups.find((item) => item.part_name === replaceGroup)?.rows ?? []}
          entries={entries}
          busy={busy}
          error={replaceError}
          onClose={() => setReplaceGroup(null)}
          onApply={async (targets) => {
            setBusy(true);
            try {
              const next = await bindComponentsBatch(
                backendBaseUrl, importId, targets, requireRevisionId(overview));
              setOverview(next);
              setError(null);
              onOverviewChange?.(next);
              onDraftInvalidated?.();
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
          targets={splitDialogTargets}
          busy={busy}
          error={splitError}
          onClose={closeSplitDialog}
          onRetry={() => void loadSplitPreview(splitDialogTargets)}
          onApply={async (targets, impactToken) => {
            setBusy(true);
            setSplitError(null);
            try {
              const applied = await applyComponentRangeSplit(
                backendBaseUrl, importId, targets, impactToken, requireRevisionId(overview)
              );
              setOverview(applied.overview);
              onOverviewChange?.(applied.overview);
              // 拆分会增删病害并复制照片候选，父页面的草稿必须重取。
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
