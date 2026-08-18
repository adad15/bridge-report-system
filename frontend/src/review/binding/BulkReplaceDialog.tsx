import { useMemo, useState } from "react";

import type { BindingReplaceInventoryEntry, BindingRow } from "../../api/importBindingApi";
import { buildReplacePreview, type ReplaceOutcome } from "./replacePreview";

// 批量查找替换。设计见
// docs/superpowers/specs/2026-07-24-bulk-binding-replace-design.md §3。
// 预览在前端算，entries 由父组件在打开对话框时按需取回，只有"应用"才打后端。

const OUTCOME_LABELS: Record<ReplaceOutcome, string> = {
  will_bind: "将绑定",
  pattern_miss: "不符合查找模式",
  not_in_inventory: "台账中无此编号",
  ambiguous: "台账中有多个同号构件",
};

export interface BulkReplaceTarget {
  part_name: string;
  component_number: string;
  bridge_component_id: string;
}

export function BulkReplaceDialog({
  partName,
  rows,
  entries,
  loading = false,
  busy,
  error,
  onRetry,
  onApply,
  onClose,
}: {
  partName: string;
  rows: BindingRow[];
  /** null 表示尚未取回。不能用空数组代替——那会让预览把所有编号判成"台账中无此编号"。 */
  entries: BindingReplaceInventoryEntry[] | null;
  loading?: boolean;
  busy: boolean;
  error?: string | null;
  onRetry?: () => void;
  onApply: (targets: BulkReplaceTarget[]) => void | Promise<void>;
  onClose: () => void;
}) {
  const [find, setFind] = useState("");
  const [replace, setReplace] = useState("");

  const preview = useMemo(
    () => (find === "" || entries === null
      ? null : buildReplacePreview(rows, entries, find, replace)),
    [rows, entries, find, replace]
  );

  const bindable = preview?.ok
    ? preview.items.filter((item) => item.outcome === "will_bind")
    : [];
  const ready = entries !== null && !loading;
  const canApply = ready && !busy && preview?.ok === true && bindable.length > 0;

  return (
    <div className="dialog-backdrop" role="presentation">
      <section
        className="workspace-dialog bulk-replace-dialog"
        role="dialog"
        aria-modal="true"
        aria-labelledby="bulk-replace-title"
      >
        <h2 id="bulk-replace-title">批量替换 · {partName}</h2>
        <p className="bulk-replace-hint">
          用 <code>*</code> 代表一段数字。例：查找 <code>第*孔桥面</code>、替换为{" "}
          <code>*#跨桥面铺装</code>。替换结果只用于查找台账构件，报告原文不会被改写。
        </p>

        <div className="bulk-replace-inputs">
          <label>
            查找
            <input
              value={find}
              disabled={!ready}
              onChange={(event) => setFind(event.target.value)}
            />
          </label>
          <label>
            替换为
            <input
              value={replace}
              disabled={!ready}
              onChange={(event) => setReplace(event.target.value)}
            />
          </label>
        </div>

        {/* 取数完成前不生成预览，也不让人输入——否则会看到一份"全都不在台账里"的假结果。 */}
        {loading ? <p className="bulk-replace-summary">正在加载台账构件…</p> : null}
        {!loading && entries === null && !error ? (
          <p className="error-text" role="alert">
            台账构件加载失败。
            {onRetry ? <button type="button" onClick={onRetry}>重试</button> : null}
          </p>
        ) : null}

        {preview && !preview.ok ? (
          <p className="error-text" role="alert">{preview.error}</p>
        ) : null}
        {error ? <p className="error-text" role="alert">{error}</p> : null}

        {preview?.ok ? (
          <>
            <div className="inventory-table-scroll bulk-replace-preview">
              <table className="data-table">
                <thead>
                  <tr><th>报告编号</th><th>转换后</th><th>结果</th></tr>
                </thead>
                <tbody>
                  {preview.items.map((item) => (
                    <tr key={item.componentNumber}>
                      <td>{item.componentNumber}</td>
                      <td>{item.targetNumber ?? "—"}</td>
                      <td
                        className={
                          item.outcome === "will_bind"
                            ? "bulk-replace-ok"
                            : "bulk-replace-skip"
                        }
                      >
                        {OUTCOME_LABELS[item.outcome]}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <p className="bulk-replace-summary">
              将绑定 {preview.bindableCount} 行 · 跳过 {preview.skippedCount} 行
            </p>
          </>
        ) : null}

        <div className="dialog-actions">
          <button type="button" disabled={busy} onClick={onClose}>取消</button>
          <button
            type="button"
            className="is-primary-action"
            disabled={!canApply}
            onClick={() =>
              void onApply(
                bindable.map((item) => ({
                  part_name: partName,
                  component_number: item.componentNumber,
                  bridge_component_id: item.bridgeComponentId as string,
                }))
              )
            }
          >
            {busy ? "正在应用…" : "应用"}
          </button>
        </div>
      </section>
    </div>
  );
}
