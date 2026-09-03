import { useState } from "react";
import { DownOutlined, UpOutlined } from "@ant-design/icons";

import type { PreflightResponse } from "../../api/reviewApi";
import { formatIssue } from "../preflightIssueText";

interface ReviewActionBarProps {
  onSaveDraft?: () => void;
  onPreflight?: () => void;
  onConfirmImport?: () => void;
  onCancelImport?: () => void;
  /** 重开校对态：替代"取消导入"的「放弃修改」（还原为已确认时草稿）。 */
  onAbandonReopen?: () => void;
  dirty?: boolean;
  readOnlyNotice?: string;
  onBackToBridge?: () => void;
  backLabel?: string;
  /** 已确认只读态的重开入口：存在带警告病害时对所有登录用户开放。 */
  onReopenWarnings?: () => void;
  /** 已确认只读态的重开入口：仅管理员（解锁全部修改）。 */
  onReopenFull?: () => void;
  /** 入库前检查结果。摘要显示在本栏中部，明细展开成本栏上方的浮层。 */
  preflight?: PreflightResponse | null;
  preflightTargetLabels?: ReadonlyMap<string, string>;
}

// 底部操作栏（模块 05 §7.5，布局见 2026-07-12 布局设计 §9）。本组件保持纯展示：一个按钮
// 是否可用完全取决于调用方（ReviewWorkspacePage）是否传了对应的 handler——不传 -> 禁用。
// 业务逻辑、加载中状态和只读状态全部在页面层维护（只读时页面层传 readOnlyNotice，本栏
// 整条换成只读横幅 + 返回按钮 + 可选的重开校对入口）。
export function ReviewActionBar({
  onSaveDraft,
  onPreflight,
  onConfirmImport,
  onCancelImport,
  onAbandonReopen,
  dirty = false,
  readOnlyNotice,
  onBackToBridge,
  backLabel = "返回桥梁详情",
  onReopenWarnings,
  onReopenFull,
  preflight = null,
  preflightTargetLabels = new Map(),
}: ReviewActionBarProps) {
  const [preflightExpanded, setPreflightExpanded] = useState(false);
  const preflightIssueCount = preflight
    ? preflight.blocking_errors.length + preflight.warnings.length
    : 0;
  if (readOnlyNotice) {
    return (
      <div className="review-action-bar review-action-bar-readonly">
        <p>{readOnlyNotice}</p>
        {onReopenWarnings ? (
          <button type="button" onClick={onReopenWarnings}>
            修正警告病害
          </button>
        ) : null}
        {onReopenFull ? (
          <button type="button" onClick={onReopenFull}>
            解锁全部修改
          </button>
        ) : null}
        <button type="button" onClick={onBackToBridge}>
          {backLabel}
        </button>
      </div>
    );
  }

  return (
    <div className="review-action-bar">
      <button type="button" disabled={!onBackToBridge} onClick={onBackToBridge}>
        {backLabel}
      </button>
      <span className="review-action-dirty">{dirty ? "● 有未保存的修改，请先保存草稿再进行入库前检查" : null}</span>
      {preflight ? (
        <div className={preflight.can_confirm ? "review-preflight-slot is-pass" : "review-preflight-slot is-block"}>
          {/* 明细浮在本栏上方，不把操作栏顶高，也不再另起一行。 */}
          {preflightExpanded && preflightIssueCount > 0 ? (
            <div className="review-preflight-detail">
              <ul className="review-warning-list">
                {preflight.blocking_errors.map((issue, index) => (
                  <li key={`blocking-${index}`} className="error-text">
                    {formatIssue(issue, preflightTargetLabels)}
                  </li>
                ))}
                {preflight.warnings.map((issue, index) => (
                  <li key={`warning-${index}`} className="warning-text">
                    {formatIssue(issue, preflightTargetLabels)}
                  </li>
                ))}
              </ul>
            </div>
          ) : null}
          <p className="review-preflight-line">
            <span className="review-preflight-icon" aria-hidden="true">{preflight.can_confirm ? "✓" : "⚠"}</span>
            <b>
              {preflight.can_confirm
                ? "检查通过，可以确认入库。"
                : `入库前检查：${preflight.blocking_errors.length} 个阻断项 · ${preflight.warnings.length} 个提醒`}
            </b>
            {preflightIssueCount > 0 ? (
              <button
                type="button"
                className="review-preflight-toggle"
                aria-expanded={preflightExpanded}
                aria-label={preflightExpanded ? "收起入库前检查明细" : "展开入库前检查明细"}
                onClick={() => setPreflightExpanded((value) => !value)}
              >
                {preflightExpanded ? <UpOutlined /> : <DownOutlined />}
              </button>
            ) : null}
          </p>
        </div>
      ) : null}
      {/* 取消导入 / 保存草稿 / 入库前检查 / 确认入库是同一组收口动作，成组靠右；
          左边只留「返回」。中间放不下时整组一起换行，不会拆成两截。 */}
      <div className="review-action-bar-right">
        {onAbandonReopen ? (
          <button type="button" className="review-action-cancel" onClick={onAbandonReopen}>
            放弃修改
          </button>
        ) : (
          <button type="button" className="review-action-cancel" disabled={!onCancelImport} onClick={onCancelImport}>
            取消导入
          </button>
        )}
        <button type="button" disabled={!onSaveDraft} onClick={onSaveDraft}>
          保存草稿
        </button>
        <button type="button" disabled={!onPreflight} onClick={onPreflight}>
          入库前检查
        </button>
        <button type="button" className="review-action-primary" disabled={!onConfirmImport} onClick={onConfirmImport}>
          确认年度事实入库
        </button>
      </div>
    </div>
  );
}
