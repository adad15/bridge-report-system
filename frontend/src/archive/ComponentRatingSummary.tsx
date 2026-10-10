import { Typography } from "antd";
import type { ComponentYearRating } from "../api/componentArchiveApi";
import { createLazyFeature } from "../loading/createLazyFeature";
import type { RatingPoint } from "./RatingTrendChart";

function ScoreText({ points }: { points: RatingPoint[] }) {
  return (
    <ul aria-label="年度评分数据">
      {points.map((point) => <li key={point.year}>{point.year} 年：{point.score} 分</li>)}
    </ul>
  );
}

const RatingTrendChart = createLazyFeature(
  async () => ({ default: (await import("./RatingTrendChart")).RatingTrendChart }),
  { label: "评分趋势图", minHeight: 200, placeholder: (props) => <ScoreText points={props.points} /> },
);

// Empty states and score preparation do not import the chart engine.
export function ComponentRatingSummary({ ratings }: { ratings: ComponentYearRating[] }) {
  if (ratings.length === 0) {
    return <Typography.Text type="secondary">该构件暂无年度评分记录。</Typography.Text>;
  }
  const points: RatingPoint[] = [...ratings]
    .sort((left, right) => left.inspection_year - right.inspection_year)
    .filter((rating): rating is ComponentYearRating & { score: number } => rating.score !== null)
    .map((rating) => ({
      year: String(rating.inspection_year),
      score: Math.round((rating.score + Number.EPSILON) * 100) / 100,
    }));
  if (points.length === 0) {
    return <Typography.Text type="secondary">该构件的年度评分尚未计算。</Typography.Text>;
  }
  return <RatingTrendChart points={points} />;
}
