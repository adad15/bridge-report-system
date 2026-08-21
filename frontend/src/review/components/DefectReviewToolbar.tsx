import { useEffect, useRef } from "react";

import type { DefectMatchSummary } from "../../api/defectMatchingApi";
import type {
  DefectPhotoReviewSummary,
  DefectReviewFilter,
  DefectReviewIssueFilter,
} from "../defectPhotoReviewModel";

interface DefectReviewToolbarProps {
  summary: DefectPhotoReviewSummary;
  filter: DefectReviewFilter;
  issueFilter: DefectReviewIssueFilter | null;
  search: string;
  selectedCount: number;
  selectableCount: number;
  allSelectableSelected: boolean;
  someSelectableSelected: boolean;
  viewMode: "records" | "groups";
  issueGroupCount: number;
  disabled?: boolean;
  /**
   * 评定树规则还没加载完。此时"待处理/可批量确认"算不出来——规则没到时每条病害都
   * 被记上一条"正在加载评定树规则"的问题，而"可批量确认"的判据是一条问题都没有，
   * 于是全都落进待处理。显示成 0 与 362 会让人以为真是这样，所以显示"—"。
   */
  countsPending?: boolean;
  /** 匹配结果还没回来。没有结果时没有任何一条会被判成"无匹配"，那个 0 是假的。 */
  matchCountsPending?: boolean;
  /** 重新匹配的作用域说明与预计处理条数，按钮按下前就要能看清。 */
  rematchScopeLabel: string;
  rematchCount: number;
  rematching?: boolean;
  matchError?: string | null;
  lastMatchSummary?: DefectMatchSummary | null;
  /** 上次匹配完成的时刻；结果新不新只有它说得清。 */
  lastMatchAt?: Date | null;
  /** 不传就不渲染“新增病害”——只读态和重开校对的仅警告范围没有这个入口。 */
  onAddDefect?: () => void;
  addDefectDisabled?: boolean;
  onFilterChange: (filter: DefectReviewFilter) => void;
  onIssueFilterChange: (issueFilter: DefectReviewIssueFilter | null) => void;
  onSearchChange: (search: string) => void;
  onToggleSelectAll: () => void;
  onViewModeChange: (mode: "records" | "groups") => void;
  onBatchConfirm: () => void;
  onRematch: () => void;
}

const STATUS_FILTERS: Array<{
  value: DefectReviewFilter;
  label: string;
  count: keyof DefectPhotoReviewSummary;
  /** 该计数依赖评定树规则，规则没到时算不出来。已确认与全部不看问题，不受影响。 */
  needsTreeRules?: boolean;
}> = [
  { value: "needs_attention", label: "待处理", count: "pending", needsTreeRules: true },
  { value: "batchable", label: "可批量确认", count: "batchable", needsTreeRules: true },
  { value: "confirmed", label: "已确认", count: "confirmed" },
  { value: "all", label: "全部", count: "all" },
];

// 组合病害、多候选和无结果是三种完全不同的人工动作，顶部就要能分开点进去。
const ISSUE_FILTERS: Array<{
  value: DefectReviewIssueFilter;
  label: string;
  count: keyof DefectPhotoReviewSummary;
}> = [
  { value: "composite", label: "疑似组合病害", count: "composite" },
  { value: "candidates", label: "有多个候选", count: "candidates" },
  { value: "unmatched", label: "无匹配结果", count: "unmatched" },
];

// 疑似组合病害 / 多个候选 / 无匹配结果 已经是顶部可点的统计卡，这里不再重复一份；
// 下拉框只补齐没有统计卡的那几类。
const ISSUE_FILTER_OPTIONS: Array<{ value: DefectReviewIssueFilter; label: string }> = [
  { value: "component_unbound", label: "构件未绑定" },
  { value: "scale_pending", label: "标度待选择" },
  { value: "photo_pending", label: "照片待处理" },
];

// 算不出来的计数一律显示它，而不是 0——"还不知道"和"确定是 0"必须看得出区别。
const UNKNOWN_COUNT = "—";
const PENDING_COUNT_HINT = "正在加载评定树规则，这项统计稍后给出。";
const PENDING_MATCH_HINT = "正在匹配评定树病害，这项统计稍后给出。";

// 当前筛选由统计卡设置时下拉框里没有对应项，用一个只读项如实说明，不能显示成"全部问题"。
const HEADLINE_ISSUE_VALUE = "__headline__";

function matchSummaryText(summary: DefectMatchSummary, at: Date | null | undefined): string {
  // 多个候选、疑似组合和无匹配的条数就是上一排那三个统计筹码，这里不再抄一遍；
  // 只留统计筹码说不出来的：这轮算了多少、自动定了多少、跳过多少。
  const parts = [
    `共 ${summary.processed} 条`,
    `自动匹配 ${summary.auto_bound} 条`,
    `跳过 ${summary.skipped} 条`,
  ];
  // 依赖缺失和失败是服务侧的异常，条数不为零时必须留在明面上，不能塞进悬浮提示。
  if (summary.prerequisite_missing > 0) parts.push(`依赖缺失 ${summary.prerequisite_missing} 条`);
  if (summary.failed > 0) parts.push(`失败 ${summary.failed} 条`);
  if (at) parts.push(`${at.getHours()}:${String(at.getMinutes()).padStart(2, "0")} 更新`);
  return parts.join(" · ");
}

export function DefectReviewToolbar({
  summary,
  filter,
  issueFilter,
  search,
  selectedCount,
  selectableCount,
  allSelectableSelected,
  someSelectableSelected,
  viewMode,
  issueGroupCount,
  disabled = false,
  countsPending = false,
  matchCountsPending = false,
  rematchScopeLabel,
  rematchCount,
  rematching = false,
  matchError = null,
  lastMatchSummary = null,
  lastMatchAt = null,
  onAddDefect,
  addDefectDisabled = false,
  onFilterChange,
  onIssueFilterChange,
  onSearchChange,
  onToggleSelectAll,
  onViewModeChange,
  onBatchConfirm,
  onRematch,
}: DefectReviewToolbarProps) {
  const selectAllRef = useRef<HTMLInputElement>(null);
  const headlineIssueActive =
    issueFilter !== null &&
    issueFilter !== "all" &&
    !ISSUE_FILTER_OPTIONS.some((option) => option.value === issueFilter);

  useEffect(() => {
    if (selectAllRef.current) {
      selectAllRef.current.indeterminate = someSelectableSelected;
    }
  }, [someSelectableSelected]);

  const clearFilters = () => {
    onFilterChange("all");
    onIssueFilterChange(null);
    onSearchChange("");
  };

  return (
    <div className="defect-review-toolbar">
      {/* 分区标题、统计与主动作同排：三者都是"这一屏在处理什么"，各占一行只是把
          工作区往下推。右侧原本空着一千多像素，正好装下三个按钮。 */}
      <div className="defect-review-toolbar-primary">
        <h2>病害与照片</h2>
        <div className="defect-review-summary" aria-label="病害校对汇总">
          {STATUS_FILTERS.map((item) => {
            const unknown = Boolean(item.needsTreeRules && countsPending);
            return (
              <button
                key={item.value}
                type="button"
                // 算不出来的筹码同时禁用：留着能点的话，点进去是一屏空列表，
                // 那和"确实一条都没有"又长得一样，等于换个地方继续误导。
                disabled={unknown}
                title={unknown ? PENDING_COUNT_HINT : undefined}
                className={filter === item.value && !issueFilter ? "active" : ""}
                onClick={() => { onIssueFilterChange(null); onFilterChange(item.value); }}
              >
                <span>{item.label}</span>
                <strong>{unknown ? UNKNOWN_COUNT : summary[item.count]}</strong>
              </button>
            );
          })}
          <span className="defect-review-summary-divider" aria-hidden="true" />
          {ISSUE_FILTERS.map((item) => {
            const unknown = Boolean(matchCountsPending);
            return (
              <button
                key={item.value}
                type="button"
                disabled={unknown}
                title={unknown ? PENDING_MATCH_HINT : undefined}
                className={`defect-review-issue-stat ${issueFilter === item.value ? "active" : ""}`}
                onClick={() => {
                  onFilterChange("all");
                  onIssueFilterChange(issueFilter === item.value ? null : item.value);
                }}
              >
                <span>{item.label}</span>
                <strong>{unknown ? UNKNOWN_COUNT : summary[item.count]}</strong>
              </button>
            );
          })}
        </div>
        {lastMatchSummary ? (
          <p className="defect-match-summary">{matchSummaryText(lastMatchSummary, lastMatchAt)}</p>
        ) : null}
        <div className="defect-review-toolbar-actions">
          {onAddDefect ? (
            <button type="button" disabled={addDefectDisabled} onClick={onAddDefect}>新增病害</button>
          ) : null}
          <button
            type="button"
            disabled={disabled || rematching}
            title={`将对${rematchScopeLabel}的 ${rematchCount} 条未确认病害重新匹配`}
            onClick={onRematch}
          >
            {rematching ? "匹配中…" : `重新匹配（${rematchScopeLabel} ${rematchCount}）`}
          </button>
          <button
            type="button"
            className="review-action-primary"
            disabled={disabled || selectedCount === 0}
            onClick={onBatchConfirm}
          >
            批量确认（{selectedCount}）
          </button>
        </div>
      </div>
      {matchError ? (
        <p className="form-error" role="alert">
          {matchError}
          <button type="button" onClick={onRematch} disabled={rematching}>重试</button>
        </p>
      ) : null}
      <div className="defect-review-filters">
        <div className="defect-review-mode" role="group" aria-label="病害查看方式">
          <button
            type="button"
            aria-pressed={viewMode === "records"}
            onClick={() => onViewModeChange("records")}
          >
            逐条查看
          </button>
          <button
            type="button"
            aria-pressed={viewMode === "groups"}
            onClick={() => onViewModeChange("groups")}
          >
            问题分组（{issueGroupCount}）
          </button>
        </div>
        <input
          className="defect-review-search"
          aria-label="搜索病害"
          placeholder="搜索构件、位置、病害或照片编号"
          value={search}
          onChange={(event) => onSearchChange(event.target.value)}
        />
        <select
          aria-label="问题类型"
          value={headlineIssueActive ? HEADLINE_ISSUE_VALUE : issueFilter ?? ""}
          onChange={(event) =>
            onIssueFilterChange(
              (event.target.value || null) as DefectReviewIssueFilter | null,
            )}
        >
          <option value="">全部问题</option>
          {headlineIssueActive ? (
            <option value={HEADLINE_ISSUE_VALUE} disabled>已按上方统计筛选</option>
          ) : null}
          {ISSUE_FILTER_OPTIONS.map((option) => (
            <option key={option.value} value={option.value}>{option.label}</option>
          ))}
        </select>
        <button type="button" onClick={clearFilters}>清除筛选</button>
        {/* 全选是批量确认的前置动作，右对齐到与它同一条竖线上。 */}
        <label
          className="defect-review-select-all"
          title={`选择当前筛选结果中可批量确认的 ${selectableCount} 条病害`}
        >
          <input
            ref={selectAllRef}
            type="checkbox"
            checked={allSelectableSelected}
            disabled={disabled || selectableCount === 0}
            onChange={onToggleSelectAll}
          />
          <span>全选筛选内可确认项（{selectableCount}）</span>
        </label>
      </div>
    </div>
  );
}
