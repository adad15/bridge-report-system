import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { ComponentRatingCandidate, Ratings } from "../../contracts/annualInspection";
import { RatingsSection } from "./RatingsSection";

function makeComponentRating(overrides: Partial<ComponentRatingCandidate> = {}): ComponentRatingCandidate {
  return {
    candidate_id: "component_rating_0001",
    component_ref: { structure_part: "上部结构", component_name: "上部承重构件", component_alias: "2-1#板" },
    source_score: 55.81,
    calculated_score: 55.80761184457488,
    confirmed_score: 55.81,
    score_validation_status: "一致",
    score_resolution_reason: null,
    deduction_defect_candidate_ids: ["defect_0001"],
    calculation_details: { standard: "JTG/T H21-2011 4.1.1", ordered_deductions: [35, 20], rounding_scale: 2 },
    review_status: "待确认",
    warnings: [],
    ...overrides,
  };
}

function makeRatings(componentRatings: ComponentRatingCandidate[]): Ratings {
  return {
    overall: { total_score: 85.61, overall_grade: "2类", source_ref: {}, confidence: 0.9, review_status: "待确认" },
    structure_parts: [],
    evaluation_parts: [],
    component_ratings: componentRatings,
    warnings: [],
  };
}

describe("RatingsSection component ratings", () => {
  it("renders the component rating row with source, rounded calculated score, and status badge", () => {
    render(<RatingsSection ratings={makeRatings([makeComponentRating()])} dispatch={vi.fn()} />);

    expect(screen.getByText("构件评分（JTG/T H21-2011 4.1.1 复算校验）")).toBeInTheDocument();
    expect(screen.getByText("2-1#板")).toBeInTheDocument();
    expect(screen.getAllByText("55.81").length).toBeGreaterThanOrEqual(2);
    expect(screen.getByText("一致")).toBeInTheDocument();
  });

  it("requires a reason before either resolution button becomes enabled", () => {
    const dispatch = vi.fn();
    render(
      <RatingsSection
        ratings={makeRatings([
          makeComponentRating({
            source_score: 70,
            confirmed_score: null,
            score_validation_status: "不一致",
          }),
        ])}
        dispatch={dispatch}
      />
    );

    const acceptButton = screen.getByRole("button", { name: "接受Word值" });
    const adoptButton = screen.getByRole("button", { name: "采用复算值" });
    expect(acceptButton).toBeDisabled();
    expect(adoptButton).toBeDisabled();

    fireEvent.change(screen.getByLabelText("处理原因 component_rating_0001"), {
      target: { value: "现场复核后采信 Word 分值" },
    });
    expect(acceptButton).toBeEnabled();
    expect(adoptButton).toBeEnabled();

    fireEvent.click(acceptButton);
    expect(dispatch).toHaveBeenCalledWith({
      type: "resolve_component_score",
      candidateId: "component_rating_0001",
      choice: "accept_source",
      reason: "现场复核后采信 Word 分值",
    });
  });

  it("shows the stored reason and an undo button for a manually resolved rating", () => {
    const dispatch = vi.fn();
    render(
      <RatingsSection
        ratings={makeRatings([
          makeComponentRating({
            source_score: 70,
            confirmed_score: 70,
            score_validation_status: "人工接受Word值",
            score_resolution_reason: "现场复核后采信 Word 分值",
            review_status: "已修改",
          }),
        ])}
        dispatch={dispatch}
      />
    );

    expect(screen.getByText("现场复核后采信 Word 分值")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "撤销选择" }));
    expect(dispatch).toHaveBeenCalledWith({
      type: "reset_component_score_resolution",
      candidateId: "component_rating_0001",
    });
  });

  it("disables all component rating controls when the section is disabled", () => {
    render(
      <RatingsSection
        ratings={makeRatings([
          makeComponentRating({
            source_score: 70,
            confirmed_score: null,
            score_validation_status: "无法复算",
            calculated_score: null,
            calculation_details: null,
          }),
        ])}
        dispatch={vi.fn()}
        disabled
      />
    );

    expect(screen.getByLabelText("处理原因 component_rating_0001")).toBeDisabled();
    expect(screen.getByRole("button", { name: "接受Word值" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "采用复算值" })).toBeDisabled();
    expect(screen.getByLabelText("构件评分校对状态 component_rating_0001")).toBeDisabled();
  });

  it("omits the component rating table when the contract carries none", () => {
    render(<RatingsSection ratings={makeRatings([])} dispatch={vi.fn()} />);

    expect(screen.queryByText("构件评分（JTG/T H21-2011 4.1.1 复算校验）")).not.toBeInTheDocument();
  });
});
