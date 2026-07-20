import { useState } from "react";

import type { ComponentYearRating } from "../api/componentArchiveApi";

function roundScoreToTwoDecimals(value: number): number {
  return Math.round((value + Number.EPSILON) * 100) / 100;
}

function formatScore(value: number | null): string {
  return value === null ? "-" : String(roundScoreToTwoDecimals(value));
}

// 构件年度评分摘要：只读展示系统评定投影和永久计算证据。
export function ComponentRatingSummary({ ratings }: { ratings: ComponentYearRating[] }) {
  const [expandedYear, setExpandedYear] = useState<number | null>(null);

  if (ratings.length === 0) {
    return <p className="archive-empty-hint">该构件暂无年度评分记录。</p>;
  }

  return (
    <div className="table-scroll">
      <table className="data-table archive-rating-table">
        <thead>
          <tr>
            <th>年度</th>
            <th>系统评分</th>
            <th>评定来源</th>
            <th>计算证据</th>
          </tr>
        </thead>
        <tbody>
          {ratings.map((rating) => (
            <RatingRow
              key={rating.inspection_year}
              rating={rating}
              expanded={expandedYear === rating.inspection_year}
              onToggle={() =>
                setExpandedYear(expandedYear === rating.inspection_year ? null : rating.inspection_year)
              }
            />
          ))}
        </tbody>
      </table>
    </div>
  );
}

function RatingRow({
  rating,
  expanded,
  onToggle,
}: {
  rating: ComponentYearRating;
  expanded: boolean;
  onToggle: () => void;
}) {
  const orderedDeductions = rating.calculation_details.ordered_deductions ?? [];
  return (
    <>
      <tr>
        <td>{rating.inspection_year}</td>
        <td>{formatScore(rating.score)}</td>
        <td><span className="severity-badge severity-info">系统评定</span></td>
        <td>
          <button type="button" onClick={onToggle}>
            {expanded ? "收起" : "展开"}
          </button>
        </td>
      </tr>
      {expanded ? (
        <tr className="archive-rating-details-row">
          <td colSpan={4}>
            <span>
              {rating.calculation_details.standard ?? "已锁定技术评定规范包"}｜降序扣分：
              {orderedDeductions.length > 0 ? orderedDeductions.join("、") : "无"}
              ｜评定运行：{rating.assessment_run_id}
            </span>
          </td>
        </tr>
      ) : null}
    </>
  );
}
