import { useState } from "react";

import type { ComponentYearRating } from "../api/componentArchiveApi";
import { roundScoreToTwoDecimals } from "../review/componentScore";

function formatScore(value: number | null): string {
  return value === null ? "-" : String(roundScoreToTwoDecimals(value));
}

// 构件年度评分摘要（模块 06 §10）：只读展示来源分/复算分/最终分/校验状态与计算证据。
// 模块 06 不重新决定最终分值；旧 1.1 年度缺少校验明细时明确提示，绝不在页面端猜测。
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
            <th>来源分</th>
            <th>复算分</th>
            <th>最终确认分</th>
            <th>校验状态</th>
            <th>处理原因</th>
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
  if (rating.is_system_assessment) {
    return (
      <tr>
        <td>{rating.inspection_year}</td>
        <td>-</td>
        <td>{formatScore(rating.calculated_score)}</td>
        <td>{formatScore(rating.score)}</td>
        <td colSpan={3}><span className="severity-badge severity-info">系统评定</span></td>
      </tr>
    );
  }
  if (!rating.has_validation_details) {
    return (
      <tr>
        <td>{rating.inspection_year}</td>
        <td>-</td>
        <td>-</td>
        <td>{formatScore(rating.score)}</td>
        <td colSpan={3} className="archive-legacy-hint">
          历史数据缺少评分校验明细
        </td>
      </tr>
    );
  }

  const orderedDeductions = rating.calculation_details.ordered_deductions ?? [];
  return (
    <>
      <tr>
        <td>{rating.inspection_year}</td>
        <td>{formatScore(rating.source_score)}</td>
        <td title={rating.calculated_score === null ? undefined : String(rating.calculated_score)}>
          {formatScore(rating.calculated_score)}
        </td>
        <td>{formatScore(rating.score)}</td>
        <td>
          <span className="severity-badge severity-info">{rating.score_validation_status}</span>
        </td>
        <td>{rating.score_resolution_reason ?? "-"}</td>
        <td>
          <button type="button" onClick={onToggle}>
            {expanded ? "收起" : "展开"}
          </button>
        </td>
      </tr>
      {expanded ? (
        <tr className="archive-rating-details-row">
          <td colSpan={7}>
            <span>
              {rating.calculation_details.standard ?? "JTG/T H21-2011 4.1.1"}｜降序扣分：
              {orderedDeductions.length > 0 ? orderedDeductions.join("、") : "无"}
              ｜未舍入复算分：{rating.calculated_score ?? "-"}
            </span>
          </td>
        </tr>
      ) : null}
    </>
  );
}
