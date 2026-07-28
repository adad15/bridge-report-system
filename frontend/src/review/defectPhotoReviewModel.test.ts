import { describe, expect, it } from "vitest";

import type { StandardDefectCatalog } from "../api/standardsApi";
import type { RatingTreeNode } from "../api/ratingTreeApi";
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

const treeNode: RatingTreeNode = {
  id: "tree-node-crack",
  node_key: "org.bridge.defect.crack",
  parent_node_id: "tree-group",
  display_name: "裂缝",
  node_type: "defect",
  sort_order: 1,
  bridge_type_ids: ["bridge-type-1"],
  component_category_ids: ["category-1"],
  scoring_mode: "inherit_h21",
  h21_indicator_id: "indicator-crack",
  is_selectable: true,
  is_scoring: true,
  organization_note: "",
  allowed_scales: [1, 2, 3],
  h21_indicator_name: "裂缝",
  h21_source_table: "5.1.1-1",
  scale_descriptions: { "1": "完好", "2": "轻微", "3": "明显" },
  deduction_points: { "1": 0, "2": 15, "3": 30 },
  path: [],
  sources: [],
};

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
  it("allows exact and controlled-alias tree matches but blocks fuzzy suggestions", () => {
    const draft = safeDraft();
    Object.assign(draft.defects[0], {
      rating_tree_version_id: "tree-version-1",
      rating_tree_node_id: treeNode.id,
      rating_tree_match_method: "exact",
    });
    const input = {
      draft,
      defectCatalogs: [],
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      assessmentIssues: [],
    };

    expect(buildDefectPhotoReviewModel(input).rows[0].batchEligible).toBe(true);
    draft.defects[0].rating_tree_match_method = "controlled_alias";
    expect(buildDefectPhotoReviewModel(input).rows[0].batchEligible).toBe(true);
    draft.defects[0].rating_tree_match_method = "fuzzy_candidate";
    expect(buildDefectPhotoReviewModel(input).rows[0].problems.map((problem) => problem.code))
      .toContain("rating_tree_fuzzy_review_required");
  });

  it("validates tree version, component scope and allowed scales independently", () => {
    const draft = safeDraft();
    Object.assign(draft.defects[0], {
      rating_tree_version_id: "old-version",
      rating_tree_node_id: treeNode.id,
      rating_tree_match_method: "manual",
      defect_scale: 5,
    });
    const row = buildDefectPhotoReviewModel({
      draft,
      defectCatalogs: [],
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set<string>()]]),
      treeRulesReady: true,
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toEqual(expect.arrayContaining([
      "rating_tree_version_mismatch",
      "rating_tree_node_not_applicable",
      "scale_not_allowed",
    ]));
  });

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
