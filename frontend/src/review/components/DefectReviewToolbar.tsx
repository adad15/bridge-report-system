import type {
  DefectPhotoReviewSummary,
  DefectReviewFilter,
  DefectReviewProblemCategory,
} from "../defectPhotoReviewModel";

interface DefectReviewToolbarProps {
  summary: DefectPhotoReviewSummary;
  filter: DefectReviewFilter;
  problemCategory: DefectReviewProblemCategory | null;
  search: string;
  selectedCount: number;
  disabled?: boolean;
  onFilterChange: (filter: DefectReviewFilter) => void;
  onProblemCategoryChange: (category: DefectReviewProblemCategory | null) => void;
  onSearchChange: (search: string) => void;
  onBatchConfirm: () => void;
}

const FILTERS: Array<{ value: DefectReviewFilter; label: string; count: keyof DefectPhotoReviewSummary }> = [
  { value: "needs_attention", label: "需处理", count: "needs_attention" },
  { value: "batchable", label: "可批量确认", count: "batchable" },
  { value: "confirmed", label: "已确认", count: "confirmed" },
  { value: "all", label: "全部", count: "all" },
];

export function DefectReviewToolbar({
  summary,
  filter,
  problemCategory,
  search,
  selectedCount,
  disabled = false,
  onFilterChange,
  onProblemCategoryChange,
  onSearchChange,
  onBatchConfirm,
}: DefectReviewToolbarProps) {
  const clearFilters = () => {
    onFilterChange("all");
    onProblemCategoryChange(null);
    onSearchChange("");
  };

  return (
    <div className="defect-review-toolbar">
      <div className="defect-review-summary" aria-label="病害校对汇总">
        {FILTERS.map((item) => (
          <button
            key={item.value}
            type="button"
            className={filter === item.value ? "active" : ""}
            onClick={() => onFilterChange(item.value)}
          >
            <span>{item.label}</span>
            <strong>{summary[item.count]}</strong>
          </button>
        ))}
      </div>
      <div className="defect-review-filters">
        <input
          aria-label="搜索病害"
          placeholder="搜索构件、位置、病害或照片编号"
          value={search}
          onChange={(event) => onSearchChange(event.target.value)}
        />
        <select
          aria-label="问题类型"
          value={problemCategory ?? ""}
          onChange={(event) =>
            onProblemCategoryChange(
              (event.target.value || null) as DefectReviewProblemCategory | null,
            )}
        >
          <option value="">全部问题</option>
          <option value="component">构件</option>
          <option value="defect_type">病害类型</option>
          <option value="scale">标度</option>
          <option value="photo">照片</option>
          <option value="other">其他</option>
        </select>
        <button type="button" onClick={clearFilters}>清除筛选</button>
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
