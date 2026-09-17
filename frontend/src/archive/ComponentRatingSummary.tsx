import { Line } from "@ant-design/charts";
import { Typography, theme } from "antd";

import type { ComponentYearRating } from "../api/componentArchiveApi";

function roundScoreToTwoDecimals(value: number): number {
  return Math.round((value + Number.EPSILON) * 100) / 100;
}

interface RatingPoint {
  year: string;
  score: number;
}

// 构件年度评分：年份—评分折线图，只读展示系统评定投影。
export function ComponentRatingSummary({ ratings }: { ratings: ComponentYearRating[] }) {
  // 图表画在 canvas 上读不到 CSS，颜色只能从主题 Token 取值传进去。
  const { token } = theme.useToken();
  if (ratings.length === 0) {
    return <Typography.Text type="secondary">该构件暂无年度评分记录。</Typography.Text>;
  }

  const points: RatingPoint[] = [...ratings]
    .sort((left, right) => left.inspection_year - right.inspection_year)
    .filter((rating): rating is ComponentYearRating & { score: number } => rating.score !== null)
    .map((rating) => ({
      year: String(rating.inspection_year),
      score: roundScoreToTwoDecimals(rating.score),
    }));

  if (points.length === 0) {
    return <Typography.Text type="secondary">该构件的年度评分尚未计算。</Typography.Text>;
  }

  // 各年评分往往只差几分，直接用 0-100 会把折线压成一条平线；
  // 这里按实际跨度留白，跨度为 0（各年持平）时也给一段固定余量。
  const values = points.map((point) => point.score);
  const minimum = Math.min(...values);
  const maximum = Math.max(...values);
  const margin = Math.max(2, (maximum - minimum) * 0.4);
  const domainMin = Math.max(0, Math.floor(minimum - margin));
  const domainMax = Math.min(100, Math.ceil(maximum + margin));

  return (
    <div aria-label="构件年度评分趋势">
      <Line
        data={points}
        xField="year"
        yField="score"
        height={200}
        autoFit
        scale={{ y: { domainMin, domainMax, nice: false } }}
        style={{ lineWidth: 2, stroke: token.colorPrimary }}
        point={{
          shapeField: "circle",
          sizeField: 4,
          style: { fill: token.colorPrimary, stroke: token.colorBgContainer, lineWidth: 1.5 },
        }}
        label={{
          text: "score",
          position: "top",
          dy: -10,
          // fillOpacity 必须显式给：G2 默认把标签压到约 45% 不透明度，
          // 只调 fill 会被这层透明度吃掉，看起来永远是浅灰。
          style: { fontSize: token.fontSizeSM, fontWeight: 600, fill: token.colorText, fillOpacity: 1 },
        }}
        // 坐标是读图的基准，不能比数据还淡。
        //
        // G2 默认把坐标标签压到约 45% 不透明度，只改 labelFill 会被这层透明度吃掉
        // （实测深灰 #334155 合成出来是 rgb(163,169,178)）。解开它的键是 labelOpacity；
        // labelFillOpacity 在坐标轴上不生效——那是标记标签(label.style.fillOpacity)的写法。
        axis={{
          x: {
            title: false,
            labelFontSize: token.fontSizeSM,
            labelFill: token.colorTextSecondary,
            labelOpacity: 1,
            lineStroke: token.colorBorder,
            tickStroke: token.colorBorder,
          },
          y: {
            title: false,
            labelFontSize: token.fontSizeSM,
            labelFill: token.colorTextSecondary,
            labelOpacity: 1,
            gridStroke: token.colorBorderSecondary,
            gridLineWidth: 1,
          },
        }}
        tooltip={{ title: (datum: RatingPoint) => `${datum.year} 年`, items: [{ channel: "y", name: "评分" }] }}
      />
    </div>
  );
}
