import { describe, expect, it } from "vitest";

import type { StandardDefectCatalog } from "../api/standardsApi";
import { data as completeData } from "./testFixtures";
import { buildDefectPhotoReviewModel } from "./defectPhotoReviewModel";

const catalogs: StandardDefectCatalog[] = [{
  id: "catalog-1",
  applicable_component_ids: ["category-1"],
  source_clause: "5.1.1",
  indicators: [{
    id: "indicator-crack",
    name: "裂缝",
    allowed_scales: [1, 2, 3],
    deduction_rule_id: "rule-1",
    source_table: "5.1.1-1",
  }],
}];

function safeDraft() {
  const draft = completeData();
  const defect = draft.defects[0];
  defect.bridge_component_id = "component-1";
  defect.standard_component_category_id = "category-1";
  defect.standard_defect_indicator_id = "indicator-crack";
  defect.defect_scale = 2;
  defect.review_status = "待确认";
  defect.group_review_status = "待确认";
  defect.photo_references = [{
    photo_number: "2.1-1",
    resolution: "matched",
    photo_candidate_id: "photo_0001",
    resolved_defect_candidate_id: "defect_0001",
    review_note: null,
  }];
  draft.photos[0].match_status = "已确认";
  draft.photos[0].review_status = "已确认";
  return draft;
}

describe("buildDefectPhotoReviewModel", () => {
  it("marks a complete group as safe for batch confirmation", () => {
    const model = buildDefectPhotoReviewModel({
      draft: safeDraft(),
      defectCatalogs: catalogs,
      assessmentIssues: [],
    });

    expect(model.summary).toEqual({
      all: 1,
      batchable: 1,
      needs_attention: 0,
      confirmed: 0,
    });
    expect(model.safeCandidateIds.has("defect_0001")).toBe(true);
  });

  it("treats one archived high-confidence photo as an atomic batch-confirm candidate", () => {
    const draft = safeDraft();
    draft.defects[0].photo_references[0] = {
      photo_number: "2.1-1",
      resolution: "pending",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    };
    draft.photos[0].match_status = "高置信候选";
    draft.photos[0].review_status = "待确认";

    const row = buildDefectPhotoReviewModel({
      draft,
      defectCatalogs: catalogs,
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems).toEqual([]);
    expect(row.batchEligible).toBe(true);
  });

  it("suggests one exact applicable indicator without confirming it", () => {
    const draft = safeDraft();
    draft.defects[0].standard_defect_indicator_id = null;
    const row = buildDefectPhotoReviewModel({
      draft,
      defectCatalogs: catalogs,
      assessmentIssues: [],
    }).rows[0];

    expect(row.suggestedIndicator?.id).toBe("indicator-crack");
    expect(row.problems.map((problem) => problem.code)).toContain("indicator_required");
    expect(row.batchEligible).toBe(false);
  });

  it("blocks repeated photo references and searches by photo number", () => {
    const draft = safeDraft();
    draft.defects.push({
      ...draft.defects[0],
      candidate_id: "defect_0002",
      component_number: "2-2#梁",
      photo_references: [{
        photo_number: "2.1-1",
        resolution: "pending",
        photo_candidate_id: null,
        resolved_defect_candidate_id: null,
        review_note: null,
      }],
    });
    const model = buildDefectPhotoReviewModel({
      draft,
      defectCatalogs: catalogs,
      assessmentIssues: [],
      filter: "needs_attention",
      problemCategory: "photo",
      search: "2.1-1",
    });

    expect(model.rows).toHaveLength(2);
    expect(model.rows.every((row) =>
      row.problems.some((problem) => problem.code === "photo_number_conflict")
    )).toBe(true);
  });
});
