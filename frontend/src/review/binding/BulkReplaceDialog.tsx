import { useEffect, useRef, useState } from "react";

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

/* 查找串停顿多久就去取一次预览。太短会把每个按键都打成一次后端请求，
   太长又会让人以为没反应；400ms 是"手停下来"的常见阈值。 */
const PREVIEW_DEBOUNCE_MS = 400;

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
  onClearPlan,
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
  /** 查找串清空时丢掉上一份计划，免得空条件下还挂着旧预览。 */
  onClearPlan: () => void;
  onApply: (planToken: string) => void | Promise<void>;
  onClose: () => void;
}) {
  const [find, setFind] = useState("");
  const [replace, setReplace] = useState("");

  /* 回调每次渲染都是新的匿名函数，放进依赖会让 effect 每帧重跑；用 ref 取最新的一份，
     依赖里只留真正的输入。 */
  const previewRef = useRef(onPreview);
  const clearRef = useRef(onClearPlan);
  previewRef.current = onPreview;
  clearRef.current = onClearPlan;

  // 不再要求先点一次「生成预览」：输入停下就自动去取计划。
  useEffect(() => {
    if (find.trim() === "") {
      clearRef.current();
      return;
    }
    const timer = window.setTimeout(() => {
      void previewRef.current(find, replace);
    }, PREVIEW_DEBOUNCE_MS);
    return () => window.clearTimeout(timer);
  }, [find, replace]);

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
        </div>

        {previewing ? <p className="bulk-replace-status" role="status">正在生成预览…</p> : null}

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
