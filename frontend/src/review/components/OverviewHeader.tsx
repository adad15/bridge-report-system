import { useState } from "react";

import type { ReviewResponse } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import type { ReviewCounts } from "../grouping";

interface OverviewHeaderProps {
  response: ReviewResponse;
  draft: BridgeAnnualInspectionData;
  counts: ReviewCounts;
}

function statusBadgeClass(importStatus: string): string {
  if (importStatus === "待校对") return "review-status-badge review-status-pending";
  if (importStatus === "已确认") return "review-status-badge review-status-confirmed";
  return "review-status-badge review-status-neutral";
}

// 工作台页眉（模块 05 §7.1，布局见 2026-07-12 布局设计 §5）：主行只放桥名/年度/编号/
// 状态徽章 + 三个进度徽章，其余字段和顶层解析 warnings/errors 收进"详情"折叠区。
// 计数用 counts（来自实时 draft 的 buildStatistics），不用 response.statistics
// （那是拉取时的快照，编辑后会过期）。
export function OverviewHeader({ response, draft, counts }: OverviewHeaderProps) {
  const [detailsOpen, setDetailsOpen] = useState(false);
  const { bridge, inspection_year, import_record } = response;
  // 折叠后解析问题不能被埋掉：详情按钮上挂红点计数提醒用户展开查看。
  const issueCount = draft.errors.length + draft.warnings.length;

  return (
    <header className="review-header">
      <div className="review-header-main">
        <h1>{inspection_year ? `${inspection_year.inspection_year} 年度检测` : "导入资料"} · {import_record.source_type}校对</h1>
        <span className="review-header-sub">
          {bridge.bridge_name} · {import_record.system_number}
        </span>
        <span className={statusBadgeClass(import_record.import_status)}>{import_record.import_status}</span>
        <button
          type="button"
          className="review-header-details-toggle"
          aria-expanded={detailsOpen}
          onClick={() => setDetailsOpen((open) => !open)}
        >
          详情 {detailsOpen ? "▴" : "▾"}
          {issueCount > 0 ? <span className="review-header-issue-dot">{issueCount}</span> : null}
        </button>
        <span className="review-header-spacer" />
        <span className="review-chip review-chip-warning">
          待确认 <b>{counts.pending_count}</b>
        </span>
        <span className="review-chip review-chip-success">
          已确认 <b>{counts.confirmed_count}</b>
        </span>
        <span className="review-chip">
          已忽略 <b>{counts.ignored_count}</b>
        </span>
      </div>
      {detailsOpen ? (
        <div className="review-header-details">
          <div className="review-header-details-grid">
            <span>来源类型</span>
            <span>{import_record.source_type}</span>
            <span>解析规则</span>
            <span>{import_record.importer_name ?? "-"}</span>
            <span>病害候选数量</span>
            <span>{counts.defect_count}</span>
            <span>照片候选数量</span>
            <span>{counts.photo_count}</span>
            <span>评分项数量</span>
            <span>{counts.rating_item_count}</span>
          </div>
          {draft.errors.length > 0 ? (
            <ul className="review-warning-list">
              {draft.errors.map((item, index) => (
                <li key={`error-${index}`} className="error-text">
                  {item.message}
                </li>
              ))}
            </ul>
          ) : null}
          {draft.warnings.length > 0 ? (
            <ul className="review-warning-list">
              {draft.warnings.map((item, index) => (
                <li key={`warning-${index}`} className="warning-text">
                  {item.message}
                </li>
              ))}
            </ul>
          ) : null}
        </div>
      ) : null}
    </header>
  );
}
