import { useEffect, useMemo, useState } from "react";

import {
  fetchLatestComponentInventory,
  type ComponentInventoryEntry,
  type ComponentInventoryRevision,
} from "../../api/componentInventoryApi";
import {
  bindComponent,
  bindingProgress,
  clearComponentBinding,
  fetchComponentBinding,
  markComponentMissing,
  type BindingRow,
  type ComponentBindingOverview,
} from "../../api/importBindingApi";
import { ApiError } from "../../api/apiClient";
import { backendBaseUrl } from "../../config";

const MAX_SEARCH_RESULTS = 20;

const STATUS_LABELS: Record<string, string> = {
  bound: "已绑定",
  unmatched: "未匹配",
  ambiguous: "歧义",
  missing: "已标记缺失",
};

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
        <span className="binding-progress">已处理 {progress.resolved} / 共 {progress.total}</span>
      </div>
      {error ? <p className="error-text" role="alert">{error}</p> : null}
      {overview.groups.length === 0 ? <p>本次导入没有需要绑定的病害。</p> : null}
      {overview.groups.map((group) => (
        <div className="binding-group" key={group.part_name}>
          <div className="binding-group-heading">
            <strong>{group.part_name}</strong>
            <span className="binding-group-counts">
              共 {group.total}
              {group.unmatched > 0 ? ` · 未匹配 ${group.unmatched}` : ""}
              {group.ambiguous > 0 ? ` · 歧义 ${group.ambiguous}` : ""}
              {group.missing > 0 ? ` · 缺失 ${group.missing}` : ""}
            </span>
          </div>
          {group.rows.map((row) => (
            <div className="binding-row" key={row.component_number}>
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
