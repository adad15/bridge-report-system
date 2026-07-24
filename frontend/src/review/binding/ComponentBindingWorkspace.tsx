import { useEffect, useMemo, useState } from "react";

import {
  fetchLatestComponentInventory,
  type ComponentInventoryEntry,
  type ComponentInventoryRevision,
} from "../../api/componentInventoryApi";
import {
  bindComponent,
  bindComponentsBatch,
  bindingProgress,
  clearComponentBinding,
  fetchComponentBinding,
  markComponentMissing,
  previewComponentRangeSplit,
  applyComponentRangeSplit,
  type BindingRow,
  type BindingTarget,
  type ComponentRangeSplitPreview,
  type ComponentBindingOverview,
} from "../../api/importBindingApi";
import { BulkReplaceDialog } from "./BulkReplaceDialog";
import { ComponentRangeSplitDialog } from "./ComponentRangeSplitDialog";
import { ApiError } from "../../api/apiClient";
import { backendBaseUrl } from "../../config";

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
}: {
  importId: string;
  bridgeId: string;
  onEnterReview?: () => void;
  // 每次拿到新的概览（首次加载与每次绑定操作后）都上报，供校对页侧栏同步待处理计数。
  onOverviewChange?: (overview: ComponentBindingOverview) => void;
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
  const [splitPreview, setSplitPreview] = useState<ComponentRangeSplitPreview | null>(null);
  const [splitError, setSplitError] = useState<string | null>(null);

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
    return () => {
      cancelled = true;
    };
  }, [importId, bridgeId]);

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

  async function run(action: () => Promise<ComponentBindingOverview>) {
    setBusy(true);
    try {
      const next = await action();
      setOverview(next);
      setError(null);
      onOverviewChange?.(next);
    } catch (caught) {
      setError(errorMessage(caught));
    } finally {
      setBusy(false);
    }
  }

  if (loading) return <p>正在加载构件绑定…</p>;
  if (error && !overview) return <p className="error-text" role="alert">{error}</p>;
  if (!overview) return <p>没有可绑定的病害。</p>;
  if (!overview.inventory_confirmed) {
    return (
      <p className="error-text" role="alert">
        该桥构件台账尚未确认，请先建立并确认台账后再进行构件绑定。
      </p>
    );
  }

  const allResolved = progress.total > 0 && progress.resolved === progress.total;

  return (
    <section className="component-binding-workspace" aria-labelledby="component-binding-title">
      <div className="binding-heading">
        <h3 id="component-binding-title">构件绑定</h3>
        <div className="binding-heading-tools" role="group" aria-label="按状态筛选">
          <button
            type="button"
            className="binding-split-selected"
            disabled={busy || splitSelection.size === 0}
            onClick={async () => {
              setBusy(true);
              setSplitError(null);
              try {
                setSplitPreview(await previewComponentRangeSplit(
                  backendBaseUrl, importId, [...splitSelection.values()]
                ));
              } catch (caught) {
                setError(errorMessage(caught));
              } finally {
                setBusy(false);
              }
            }}
          >
            拆分构件{splitSelection.size > 0 ? ` (${splitSelection.size})` : ""}
          </button>
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
      {error ? <p className="error-text" role="alert">{error}</p> : null}
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
                    })
                  )
                }
                onMarkMissing={() =>
                  run(() =>
                    markComponentMissing(backendBaseUrl, importId, {
                      part_name: group.part_name,
                      component_number: row.component_number,
                    })
                  )
                }
                onClear={() =>
                  run(() =>
                    clearComponentBinding(backendBaseUrl, importId, {
                      part_name: group.part_name,
                      component_number: row.component_number,
                    })
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
              const next = await bindComponentsBatch(backendBaseUrl, importId, targets);
              setOverview(next);
              setError(null);
              onOverviewChange?.(next);
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
      {splitPreview ? (
        <ComponentRangeSplitDialog
          preview={splitPreview}
          targets={[...splitSelection.values()]}
          busy={busy}
          error={splitError}
          onClose={() => { setSplitPreview(null); setSplitError(null); }}
          onApply={async (targets, impactToken) => {
            setBusy(true);
            setSplitError(null);
            try {
              const applied = await applyComponentRangeSplit(
                backendBaseUrl, importId, targets, impactToken
              );
              setOverview(applied.overview);
              onOverviewChange?.(applied.overview);
              setSplitSelection(new Map());
              setSplitPreview(null);
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
    </section>
  );
}
