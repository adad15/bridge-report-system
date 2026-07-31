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
  disabled?: boolean;
  /** 重新匹配的作用域说明与预计处理条数，按钮按下前就要能看清。 */
  rematchScopeLabel: string;
  rematchCount: number;
  rematching?: boolean;
  matchError?: string | null;
  lastMatchSummary?: DefectMatchSummary | null;
  onFilterChange: (filter: DefectReviewFilter) => void;
  onIssueFilterChange: (issueFilter: DefectReviewIssueFilter | null) => void;
  onSearchChange: (search: string) => void;
  onBatchConfirm: () => void;
  onRematch: () => void;
}

const STATUS_FILTERS: Array<{
  value: DefectReviewFilter;
  label: string;
  count: keyof DefectPhotoReviewSummary;
}> = [
  { value: "needs_attention", label: "待处理", count: "pending" },
  { value: "batchable", label: "可批量确认", count: "batchable" },
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

// 当前筛选由统计卡设置时下拉框里没有对应项，用一个只读项如实说明，不能显示成"全部问题"。
const HEADLINE_ISSUE_VALUE = "__headline__";

export function DefectReviewToolbar({
  summary,
  filter,
  issueFilter,
  search,
  selectedCount,
  disabled = false,
  rematchScopeLabel,
  rematchCount,
  rematching = false,
  matchError = null,
  lastMatchSummary = null,
  onFilterChange,
  onIssueFilterChange,
  onSearchChange,
  onBatchConfirm,
  onRematch,
}: DefectReviewToolbarProps) {
  const headlineIssueActive =
    issueFilter !== null &&
    issueFilter !== "all" &&
    !ISSUE_FILTER_OPTIONS.some((option) => option.value === issueFilter);

  const clearFilters = () => {
    onFilterChange("all");
    onIssueFilterChange(null);
    onSearchChange("");
  };

  return (
    <div className="defect-review-toolbar">
      <div className="defect-review-summary" aria-label="病害校对汇总">
        {STATUS_FILTERS.map((item) => (
          <button
            key={item.value}
            type="button"
            className={filter === item.value && !issueFilter ? "active" : ""}
            onClick={() => { onIssueFilterChange(null); onFilterChange(item.value); }}
          >
            <span>{item.label}</span>
            <strong>{summary[item.count]}</strong>
          </button>
        ))}
        {ISSUE_FILTERS.map((item) => (
          <button
            key={item.value}
            type="button"
            className={`defect-review-issue-stat ${issueFilter === item.value ? "active" : ""}`}
            onClick={() => {
              onFilterChange("all");
              onIssueFilterChange(issueFilter === item.value ? null : item.value);
            }}
          >
            <span>{item.label}</span>
            <strong>{summary[item.count]}</strong>
          </button>
        ))}
      </div>
      {matchError ? (
        <p className="form-error" role="alert">
          {matchError}
          <button type="button" onClick={onRematch} disabled={rematching}>重试</button>
        </p>
      ) : null}
      {lastMatchSummary ? (
        <details className="defect-match-summary">
          <summary>
            共处理 {lastMatchSummary.processed} 条：自动匹配 {lastMatchSummary.auto_bound} 条，
            多个候选 {lastMatchSummary.candidates} 条，疑似组合病害 {lastMatchSummary.composite} 条，
            无法识别 {lastMatchSummary.unmatched} 条。
          </summary>
          <p>
            依赖缺失 {lastMatchSummary.prerequisite_missing} 条，失败 {lastMatchSummary.failed} 条，
            跳过人工或已确认 {lastMatchSummary.skipped} 条。
          </p>
        </details>
      ) : null}
      <div className="defect-review-filters">
        <input
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
  );
}
