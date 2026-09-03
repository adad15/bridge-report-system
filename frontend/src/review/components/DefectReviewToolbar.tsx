import { useEffect, useRef } from "react";

import type { DefectMatchSummary } from "../../api/defectMatchingApi";
import { UNBOUND_PART_FILTER } from "../defectPhotoReviewModel";
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
  /** 当前只看哪个部件；null 表示全部。 */
  partFilter: string | null;
  onPartFilterChange: (partFilter: string | null) => void;
  /** 当前只看哪个导入病害类型；null 表示全部。 */
  defectTypeFilter: string | null;
  onDefectTypeFilterChange: (defectTypeFilter: string | null) => void;
  /** 重新匹配的作用域说明与预计处理条数，按钮按下前就要能看清。 */
  rematchScopeLabel: string;
  rematchCount: number;
  rematching?: boolean;
  matchError?: string | null;
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

// 状态与匹配问题共用一个筛选器：主界面只保留一个“全部状态”下拉，避免把同一维度
// 同时做成七个统计按钮和一组筛选控件。完整计数仍放进选项文案，业务能力没有缩水。
type SummaryCountKey = Exclude<keyof DefectPhotoReviewSummary, "parts" | "defectTypes">;

const STATUS_FILTERS: Array<{
  value: DefectReviewFilter;
  label: string;
  count: SummaryCountKey;
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
  count: SummaryCountKey;
}> = [
  { value: "composite", label: "疑似组合病害", count: "composite" },
  { value: "candidates", label: "有多个候选", count: "candidates" },
  { value: "unmatched", label: "无匹配结果", count: "unmatched" },
];

// 构件、标度和照片问题的主入口在右侧“待处理问题”面板。点进去以后，组合筛选框仍要
// 如实显示当前条件；否则数据已经筛过，控件却还写着“待处理”，用户会误以为入口失效。
const QUALITY_ISSUE_LABELS: Partial<Record<DefectReviewIssueFilter, string>> = {
  component_unbound: "构件绑定待处理",
  scale_pending: "标度信息待补充",
  photo_pending: "照片关联或编号异常",
};

// 算不出来的计数一律显示它，而不是 0——"还不知道"和"确定是 0"必须看得出区别。
const UNKNOWN_COUNT = "—";
const PENDING_COUNT_HINT = "正在加载评定树规则，这项统计稍后给出。";
const PENDING_MATCH_HINT = "正在匹配评定树病害，这项统计稍后给出。";


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
  partFilter,
  onPartFilterChange,
  defectTypeFilter,
  onDefectTypeFilterChange,
  rematchScopeLabel,
  rematchCount,
  rematching = false,
  matchError = null,
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

  useEffect(() => {
    if (selectAllRef.current) {
      selectAllRef.current.indeterminate = someSelectableSelected;
    }
  }, [someSelectableSelected]);

  const clearFilters = () => {
    onFilterChange("all");
    onIssueFilterChange(null);
    onSearchChange("");
    onPartFilterChange(null);
    onDefectTypeFilterChange(null);
  };

  const activeFilterValue = issueFilter ? `issue:${issueFilter}` : `status:${filter}`;
  const activeQualityIssueLabel = issueFilter ? QUALITY_ISSUE_LABELS[issueFilter] : undefined;
  const handleCombinedFilterChange = (value: string) => {
    if (value.startsWith("issue:")) {
      onFilterChange("all");
      onIssueFilterChange(value.slice("issue:".length) as DefectReviewIssueFilter);
      return;
    }
    onIssueFilterChange(null);
    onFilterChange(value.slice("status:".length) as DefectReviewFilter);
  };

  return (
    <div className="defect-review-toolbar">
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
            问题分组 <span>{issueGroupCount}</span>
          </button>
        </div>
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
          <span>全选可确认项（{selectableCount}）</span>
        </label>
        <button
          type="button"
          className="defect-batch-confirm-button"
          disabled={disabled || selectedCount === 0}
          onClick={onBatchConfirm}
        >
          批量确认{selectedCount > 0 ? `（${selectedCount}）` : ""}
        </button>
        {/* 空白区域中的病害类型筛选：放在批量操作与搜索之间，保留右侧原有控件宽度。 */}
        <select
          className="defect-review-type-filter"
          aria-label="按病害类型筛选"
          value={defectTypeFilter ?? ""}
          onChange={(event) => onDefectTypeFilterChange(event.target.value || null)}
        >
          <option value="">全部病害类型（{summary.all}）</option>
          {summary.defectTypes.map((item) => (
            <option key={item.name} value={item.name}>
              {item.name}（{item.count}）
            </option>
          ))}
        </select>
        <input
          className="defect-review-search"
          aria-label="搜索病害"
          placeholder="搜索构件、位置、病害或照片编号"
          value={search}
          onChange={(event) => onSearchChange(event.target.value)}
        />
        {/* 按部件筛选。与上面那排统计筹码正交：筹码筛"问题类型"，这里筛"部件"，
            两者可以叠加（"只看铰缝里无匹配的"）。选项按走查顺序排、与列表顺序一致，
            只列这份草稿里真的出现过的部件。 */}
        <select
          aria-label="按部件筛选"
          value={partFilter ?? ""}
          onChange={(event) => onPartFilterChange(event.target.value || null)}
        >
          <option value="">全部部件</option>
          {summary.parts.map((part) => (
            <option key={part.name} value={part.name}>
              {part.name === UNBOUND_PART_FILTER ? "未绑定构件" : part.name}（{part.count}）
            </option>
          ))}
        </select>
        <select
          aria-label="按状态筛选"
          value={activeFilterValue}
          onChange={(event) => handleCombinedFilterChange(event.target.value)}
        >
          <optgroup label="处理状态">
            {STATUS_FILTERS.map((item) => {
              const unknown = Boolean(item.needsTreeRules && countsPending);
              return (
                <option key={item.value} value={`status:${item.value}`} disabled={unknown}>
                  {item.value === "all" ? "全部状态" : item.label}（{unknown ? UNKNOWN_COUNT : summary[item.count]}）
                </option>
              );
            })}
          </optgroup>
          <optgroup label="匹配问题">
            {ISSUE_FILTERS.map((item) => (
              <option key={item.value} value={`issue:${item.value}`} disabled={matchCountsPending}>
                {item.label}（{matchCountsPending ? UNKNOWN_COUNT : summary[item.count]}）
              </option>
            ))}
          </optgroup>
          {activeQualityIssueLabel ? (
            <optgroup label="当前校对问题">
              <option value={`issue:${issueFilter}`}>{activeQualityIssueLabel}</option>
            </optgroup>
          ) : null}
        </select>
        {filter !== "all" || issueFilter || search || partFilter || defectTypeFilter ? (
          <button type="button" onClick={clearFilters}>清除筛选</button>
        ) : null}
        <div className="defect-review-toolbar-actions">
          {onAddDefect ? (
            <button type="button" disabled={addDefectDisabled} onClick={onAddDefect}>新增病害</button>
          ) : null}
          <button
            type="button"
            aria-label="重新匹配"
            className="defect-rematch-button"
            disabled={disabled || rematching}
            title={`将对${rematchScopeLabel}的 ${rematchCount} 条未确认病害重新匹配`}
            onClick={onRematch}
          >{rematching ? "匹配中…" : "重新匹配"}</button>
        </div>
      </div>
    </div>
  );
}
