import { render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import type { ComponentYearRating } from "../api/componentArchiveApi";
import { ComponentRatingSummary } from "./ComponentRatingSummary";
const { chartImported } = vi.hoisted(() => ({ chartImported: vi.fn() }));
vi.mock("./RatingTrendChart", () => {
  chartImported();
  return { RatingTrendChart: ({ points }: { points: { year: string; score: number }[] }) =>
    <div aria-label="测试趋势图">{JSON.stringify(points)}</div> };
});
const rating = (year: number, score: number | null): ComponentYearRating => ({
  inspection_year: year, score, calculation_details: {}, assessment_run_id: "run",
});
describe("rating summary loading", () => {
  it("does not import charts for empty or uncalculated ratings", () => {
    const view = render(<ComponentRatingSummary ratings={[]} />);
    expect(screen.getByText("该构件暂无年度评分记录。")).toBeInTheDocument();
    view.rerender(<ComponentRatingSummary ratings={[rating(2026, null)]} />);
    expect(screen.getByText("该构件的年度评分尚未计算。")).toBeInTheDocument();
    expect(chartImported).not.toHaveBeenCalled();
  });
  it("sorts, filters and rounds without mutating its input", async () => {
    const ratings = [rating(2026, 83.456), rating(2023, null), rating(2025, 90)];
    render(<ComponentRatingSummary ratings={ratings} />);
    const chart = await screen.findByLabelText("测试趋势图");
    expect(JSON.parse(chart.textContent!)).toEqual([{ year: "2025", score: 90 }, { year: "2026", score: 83.46 }]);
    expect(ratings[0].score).toBe(83.456);
    expect(ratings[0].inspection_year).toBe(2026);
  });
});
