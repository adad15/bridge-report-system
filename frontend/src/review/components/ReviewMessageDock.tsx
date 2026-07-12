import { useState } from "react";

import type { ApiErrorIssue } from "../../api/apiClient";
import type { PreflightIssue, PreflightResponse } from "../../api/reviewApi";

// 保存草稿 / 入库前检查 / 确认入库失败后展示给用户的提示；成功也复用同一个状态
// （kind="success"）显示"已保存"这类短暂反馈。issues 只有 400 契约校验失败
// （code=contract_validation_failed）时后端才会填，逐条列出 {path, message}。
export interface SaveMessageState {
  kind: "success" | "error";
  text: string;
  issues?: ApiErrorIssue[];
}

interface ReviewMessageDockProps {
  saveMessage: SaveMessageState | null;
  preflight: PreflightResponse | null;
  onDismissSaveMessage: () => void;
}

function formatIssue(issue: PreflightIssue): string {
  return `${issue.code}：${issue.message}${issue.target_candidate_id ? `（${issue.target_candidate_id}）` : ""}`;
}

// 停靠在底部操作栏上方的消息面板：保存结果 + 入库前检查结果，让反馈永远出现在触发它的
// 按钮旁边。纯展示组件——成功消息的自动消失定时器、preflight 的失效时机都归页面层管。
export function ReviewMessageDock({ saveMessage, preflight, onDismissSaveMessage }: ReviewMessageDockProps) {
  const [expanded, setExpanded] = useState(false);

  if (!saveMessage && !preflight) return null;

  const preflightIssueCount = preflight ? preflight.blocking_errors.length + preflight.warnings.length : 0;

  return (
    <div className="review-message-dock">
      {saveMessage ? (
        <div className={saveMessage.kind === "error" ? "review-dock-row review-dock-error" : "review-dock-row review-dock-success"}>
          <div className="review-dock-body">
            <p className={saveMessage.kind === "error" ? "error-text" : undefined}>{saveMessage.text}</p>
            {saveMessage.issues && saveMessage.issues.length > 0 ? (
              <ul className="review-warning-list">
                {saveMessage.issues.map((issue, index) => (
                  <li key={`${issue.path}-${index}`} className="error-text">
                    {issue.path}: {issue.message}
                  </li>
                ))}
              </ul>
            ) : null}
          </div>
          {saveMessage.kind === "error" ? (
            <button type="button" className="review-dock-close" aria-label="关闭消息" onClick={onDismissSaveMessage}>
              ×
            </button>
          ) : null}
        </div>
      ) : null}
      {preflight ? (
        <div className={preflight.can_confirm ? "review-dock-row review-dock-pass" : "review-dock-row review-dock-block"}>
          <div className="review-dock-body">
            <p>
              <b>
                {preflight.can_confirm
                  ? "检查通过，可以确认入库。"
                  : `入库前检查未通过：${preflight.blocking_errors.length} 个阻断项、${preflight.warnings.length} 个警告。`}
              </b>
            </p>
            {expanded ? (
              <ul className="review-warning-list">
                {preflight.blocking_errors.map((issue, index) => (
                  <li key={`blocking-${index}`} className="error-text">
                    {formatIssue(issue)}
                  </li>
                ))}
                {preflight.warnings.map((issue, index) => (
                  <li key={`warning-${index}`} className="warning-text">
                    {formatIssue(issue)}
                  </li>
                ))}
              </ul>
            ) : null}
          </div>
          {preflightIssueCount > 0 ? (
            <button type="button" className="review-dock-toggle" onClick={() => setExpanded((value) => !value)}>
              {expanded ? "收起 ▴" : "展开 ▾"}
            </button>
          ) : null}
        </div>
      ) : null}
    </div>
  );
}
