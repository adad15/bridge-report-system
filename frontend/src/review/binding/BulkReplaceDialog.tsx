import { useState } from "react";

import type { ResolutionPlanPreview } from "../../api/resolutionApi";

// 批量查找替换。设计见
// docs/superpowers/specs/2026-07-24-bulk-binding-replace-design.md §3 与
// docs/superpowers/specs/2026-08-27-import-component-rating-resolution-separation-design.md §13.3。
//
// 5.0 起**预览由后端生成**：前端把查找/替换串交上去换一份计划，应用时只提交
// plan token，不提交自己算出来的结果集合。这样"用户看到的计划"与"实际执行的计划"
// 天然是同一份——此前两边各算一次，规则一分叉就会出现"预览说能绑、后端却判无此编号"。

const OUTCOME_LABELS: Record<string, string> = {
  will_bind: "将绑定",
  will_clear: "将清除绑定",
  will_repoint: "将重指版本",
  skipped: "跳过",
  blocked: "阻断",
};

// 行级原因码由后端给，前端只做展示措辞，不自己判断为什么跳过。
const REASON_LABELS: Record<string, string> = {
  pattern_not_matched: "不符合查找模式",
  component_not_found: "台账中无此编号",
  component_ambiguous: "台账中有多个同号构件",
  group_already_resolved: "已绑定或已标记缺失，不参与",
  not_a_range: "该编号不是可展开的构件范围",
};

function outcomeLabel(row: ResolutionPlanPreview["rows"][number]): string {
  if (row.outcome === "will_bind") return OUTCOME_LABELS.will_bind;
  return REASON_LABELS[row.reason_code] ?? row.reason_message ??
    OUTCOME_LABELS[row.outcome] ?? row.outcome;
}

export function BulkReplaceDialog({
  partName,
  plan,
  previewing,
  busy,
  error,
  onPreview,
  onApply,
  onClose,
}: {
  partName: string;
  /** null 表示还没预览过。计划一律来自后端，前端不构造。 */
  plan: ResolutionPlanPreview | null;
  previewing: boolean;
  busy: boolean;
  error?: string | null;
  onPreview: (find: string, replace: string) => void | Promise<void>;
  onApply: (planToken: string) => void | Promise<void>;
  onClose: () => void;
}) {
  const [find, setFind] = useState("");
  const [replace, setReplace] = useState("");

  const canPreview = find.trim() !== "" && !previewing && !busy;
  const canApply = plan !== null && plan.will_apply_count > 0 && !busy && !previewing;

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
              disabled={busy}
              onChange={(event) => setFind(event.target.value)}
            />
          </label>
          <label>
            替换为
            <input
              value={replace}
              disabled={busy}
              onChange={(event) => setReplace(event.target.value)}
            />
          </label>
          <button
            type="button"
            disabled={!canPreview}
            onClick={() => void onPreview(find, replace)}
          >
            {previewing ? "正在生成预览…" : "生成预览"}
          </button>
        </div>

        {error ? <p className="error-text" role="alert">{error}</p> : null}

        {plan ? (
          <>
            <div className="inventory-table-scroll bulk-replace-preview">
              <table className="data-table">
                <thead>
                  <tr><th>报告编号</th><th>转换后</th><th>结果</th></tr>
                </thead>
                <tbody>
                  {plan.rows.map((row) => (
                    <tr key={row.group_id}>
                      <td>{row.source_component_number}</td>
                      <td>{row.resolved_numbers[0] ?? "—"}</td>
                      <td
                        className={
                          row.outcome === "will_bind"
                            ? "bulk-replace-ok"
                            : "bulk-replace-skip"
                        }
                      >
                        {outcomeLabel(row)}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <p className="bulk-replace-summary">
              将绑定 {plan.will_apply_count} 行 · 跳过 {plan.skipped_count} 行
              {plan.blocked_count > 0 ? ` · 阻断 ${plan.blocked_count} 行` : ""}
            </p>
          </>
        ) : null}

        <div className="dialog-actions">
          <button type="button" disabled={busy} onClick={onClose}>取消</button>
          <button
            type="button"
            className="is-primary-action"
            disabled={!canApply}
            onClick={() => plan && void onApply(plan.plan_token)}
          >
            {busy ? "正在应用…" : "应用"}
          </button>
        </div>
      </section>
    </div>
  );
}
