import { useMemo, useState } from "react";

import type { ComponentInventoryEntry } from "../../api/componentInventoryApi";
import type { BindingRow } from "../../api/importBindingApi";
import { buildReplacePreview, type ReplaceOutcome } from "./replacePreview";

// 批量查找替换。设计见
// docs/superpowers/specs/2026-07-24-bulk-binding-replace-design.md §3。
// 预览全在前端算（台账 entries 已在手），只有"应用"才打后端。

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
  busy,
  error,
  onApply,
  onClose,
}: {
  partName: string;
  rows: BindingRow[];
  entries: ComponentInventoryEntry[];
  busy: boolean;
  error?: string | null;
  onApply: (targets: BulkReplaceTarget[]) => void | Promise<void>;
  onClose: () => void;
}) {
  const [find, setFind] = useState("");
  const [replace, setReplace] = useState("");

  const preview = useMemo(
    () => (find === "" ? null : buildReplacePreview(rows, entries, find, replace)),
    [rows, entries, find, replace]
  );

  const bindable = preview?.ok
    ? preview.items.filter((item) => item.outcome === "will_bind")
    : [];
  const canApply = !busy && preview?.ok === true && bindable.length > 0;

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
            <input value={find} onChange={(event) => setFind(event.target.value)} />
          </label>
          <label>
            替换为
            <input value={replace} onChange={(event) => setReplace(event.target.value)} />
          </label>
        </div>

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
