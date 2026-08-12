import { describe, expect, it } from "vitest";

import type { DefectMatchCandidate, DefectMatchOutcome, DefectMatchResult } from "../api/defectMatchingApi";
import type { RatingTreeNode } from "../api/ratingTreeApi";
import { data as completeData } from "./testFixtures";
import { buildDefectPhotoReviewModel } from "./defectPhotoReviewModel";

function matchResult(
  outcome: DefectMatchOutcome,
  candidates: DefectMatchCandidate[],
): DefectMatchResult {
  return {
    candidate_id: "defect_0001",
    outcome,
    skipped: false,
    rating_tree_node_id: null,
    match_method: null,
    match_evidence: null,
    reason_code: null,
    reason_message: null,
    candidates,
  };
}

const treeNode: RatingTreeNode = {
  id: "tree-node-crack",
  node_key: "org.bridge.defect.crack",
  parent_node_id: "tree-group",
  display_number: "5.1.1-1",
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

function treeWiring() {
  return {
    ratingTreeVersionId: "tree-version-1",
    ratingTreeNodes: [treeNode],
    applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
    treeRulesReady: true,
  };
}

function safeDraft() {
  const draft = completeData();
  const defect = draft.defects[0];
  defect.bridge_component_id = "component-1";
  defect.standard_component_category_id = "category-1";
  defect.rating_tree_version_id = "tree-version-1";
  defect.rating_tree_node_id = treeNode.id;
  defect.rating_tree_match_method = "exact";
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
      ...treeWiring(),
      assessmentIssues: [],
    });

    expect(model.summary).toEqual({
      all: 1,
      pending: 0,
      batchable: 1,
      confirmed: 0,
      composite: 0,
      candidates: 0,
      unmatched: 0,
    });
    expect(model.safeCandidateIds.has("defect_0001")).toBe(true);
  });

  it("allows a non-scoring node to be confirmed from its applicable summary before details load", () => {
    const draft = safeDraft();
    const nonScoringSummary = {
      ...treeNode,
      id: "tree-node-other",
      display_name: "其它病害",
      scoring_mode: "non_scoring" as const,
      h21_indicator_id: null,
      is_scoring: false,
    };
    draft.defects[0] = {
      ...draft.defects[0],
      rating_tree_node_id: nonScoringSummary.id,
      rating_tree_match_method: "manual",
      defect_type: nonScoringSummary.display_name,
      defect_scale: null,
    };

    const row = buildDefectPhotoReviewModel({
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [],
      ratingTreeNodeSummaries: [nonScoringSummary],
      applicableTreeNodeIdsByComponent: new Map([
        ["component-1", new Set([nonScoringSummary.id])],
      ]),
      treeRulesReady: true,
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems).toEqual([]);
    expect(row.confirmEligible).toBe(true);
    expect(row.batchEligible).toBe(true);
  });

  it("keeps a scoring node blocked until its scale rules are available", () => {
    const summaryWithoutScaleRules = {
      ...treeNode,
      allowed_scales: undefined,
      scale_descriptions: undefined,
    };
    const row = buildDefectPhotoReviewModel({
      draft: safeDraft(),
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [],
      ratingTreeNodeSummaries: [summaryWithoutScaleRules],
      applicableTreeNodeIdsByComponent: new Map([
        ["component-1", new Set([treeNode.id])],
      ]),
      treeRulesReady: true,
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toContain("rating_tree_node_loading");
    expect(row.confirmEligible).toBe(false);
  });

  it("allows individual confirmation of a range split without photos but keeps it out of batch confirmation", () => {
    const draft = safeDraft();
    draft.defects[0].photo_references = [];
    draft.defects[0].warnings = [{
      code: "component_range_split_review_required",
      message: "该病害由构件范围拆分，请人工核对构件、病害和照片关联。",
      severity: "warning",
      target_candidate_id: "defect_0001",
    }];

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code))
      .toEqual(["component_range_split_review_required"]);
    expect(row.confirmEligible).toBe(true);
    expect(row.batchEligible).toBe(false);
  });

  it("treats one linked archived photo as an atomic batch-confirm candidate", () => {
    const draft = safeDraft();
    draft.defects[0].photo_references[0] = {
      photo_number: "2.1-1",
      resolution: "pending",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    };
    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems).toEqual([]);
    expect(row.batchEligible).toBe(true);
  });

  it("blocks batch confirmation until a rating tree defect is chosen", () => {
    const draft = safeDraft();
    draft.defects[0].rating_tree_node_id = null;
    draft.defects[0].rating_tree_match_method = null;
    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toContain("rating_tree_node_required");
    expect(row.batchEligible).toBe(false);
  });

  it("drops backend issues that restate a problem the frontend already derived", () => {
    const draft = safeDraft();
    draft.defects[0].rating_tree_node_id = null;
    draft.defects[0].rating_tree_match_method = null;
    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [{
        // 码名必须与 AssessmentService.cpp 实际发出的一致，别名表才去得掉重。
        code: "assessment_rating_tree_node_required",
        message: "病害尚未选择当前年度评定树中的有效节点。",
        entity_type: "defect",
        entity_id: "defect_0001",
        field_path: "rating_tree_node_id",
        rule_id: "",
      }],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toEqual(["rating_tree_node_required"]);
    expect(row.batchEligible).toBe(false);
  });

  it("keeps a backend issue that has no frontend equivalent", () => {
    const row = buildDefectPhotoReviewModel({
      draft: safeDraft(),
      ...treeWiring(),
      assessmentIssues: [{
        code: "assessment_defect_scale_required",
        message: "病害缺少有效的规范标度。",
        entity_type: "defect",
        entity_id: "defect_0001",
        field_path: "defect_scale",
        rule_id: "",
      }],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toEqual(["assessment_defect_scale_required"]);
  });

  it("labels controlled matches, candidates and composite defects distinctly", () => {
    const draft = safeDraft();
    Object.assign(draft.defects[0], {
      rating_tree_version_id: "tree-version-1",
      rating_tree_node_id: treeNode.id,
      rating_tree_match_method: "controlled_keyword",
    });
    const treeInput = {
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      assessmentIssues: [],
    };

    const bound = buildDefectPhotoReviewModel(treeInput).rows[0];
    expect(bound.matchState).toBe("auto_bound");
    expect(bound.matchLabel).toBe("自动匹配：裂缝");
    // 自动绑定不确认：仍然停留在待确认，只是满足条件后可批量确认。
    expect(bound.defect.group_review_status).toBe("待确认");
    expect(bound.batchEligible).toBe(true);

    draft.defects[0].rating_tree_node_id = null;
    draft.defects[0].rating_tree_match_method = null;
    const composite = buildDefectPhotoReviewModel({
      ...treeInput,
      matchResults: new Map([["defect_0001", matchResult("composite", [
        { rating_tree_node_id: treeNode.id, display_name: "裂缝", match_method: "controlled_alias", evidence: "命中别名" },
        { rating_tree_node_id: "tree-node-water", display_name: "水损", match_method: "controlled_keyword", evidence: "命中关键词" },
      ])]]),
    }).rows[0];
    expect(composite.matchState).toBe("composite");
    expect(composite.matchLabel).toBe("疑似组合病害");
    expect(composite.batchEligible).toBe(false);
    expect(composite.matchCandidates).toHaveLength(2);

    const candidates = buildDefectPhotoReviewModel({
      ...treeInput,
      matchResults: new Map([["defect_0001", matchResult("candidates", [
        { rating_tree_node_id: treeNode.id, display_name: "裂缝", match_method: "fuzzy_candidate", evidence: "文字相似" },
      ])]]),
    }).rows[0];
    expect(candidates.matchState).toBe("candidates");
    expect(candidates.matchLabel).toBe("候选 1 项");
    expect(candidates.batchEligible).toBe(false);
  });

  it("keeps missing prerequisites and matcher failures out of the plain unmatched bucket", () => {
    const draft = safeDraft();
    draft.defects[0].rating_tree_node_id = null;
    draft.defects[0].rating_tree_match_method = null;
    const treeInput = {
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      assessmentIssues: [],
    };

    const missing = buildDefectPhotoReviewModel({
      ...treeInput,
      matchResults: new Map([["defect_0001", {
        ...matchResult("prerequisite_missing", []),
        reason_code: "component_not_bound",
        reason_message: "该病害尚未绑定实际构件。",
      }]]),
    });
    expect(missing.rows[0].matchState).toBe("prerequisite_missing");
    expect(missing.summary.unmatched).toBe(0);
    expect(missing.rows[0].problems.map((problem) => problem.code))
      .toContain("rating_tree_prerequisite_missing");

    const failed = buildDefectPhotoReviewModel({
      ...treeInput,
      matchResults: new Map([["defect_0001", {
        ...matchResult("service_error", []),
        reason_code: "matcher_failed",
        reason_message: "匹配服务执行失败，请稍后重试。",
      }]]),
    });
    expect(failed.rows[0].matchState).toBe("service_error");
    expect(failed.summary.unmatched).toBe(0);
    expect(failed.rows[0].problems.map((problem) => problem.code))
      .toContain("rating_tree_matcher_failed");
  });

  // 列表每行只剩一个徽标，所以终态必须由它自己说出来，不能靠另一个状态徽标兜底。
  it("says an ignored defect is ignored on the single row badge", () => {
    const draft = safeDraft();
    draft.defects[0].review_status = "已忽略";
    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.status).toBe("ignored");
    expect(row.matchState).toBe("ignored");
    expect(row.matchLabel).toBe("已忽略");
  });

  it("keeps confirmed rows readable from the same single badge", () => {
    const draft = safeDraft();
    draft.defects[0].group_review_status = "已确认";
    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.status).toBe("confirmed");
    expect(row.matchLabel).toBe("已确认：裂缝");
  });

  it("never lets an automatic result relabel a manual or confirmed record", () => {
    const draft = safeDraft();
    Object.assign(draft.defects[0], {
      rating_tree_version_id: "tree-version-1",
      rating_tree_node_id: treeNode.id,
      rating_tree_match_method: "manual",
    });
    const row = buildDefectPhotoReviewModel({
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      assessmentIssues: [],
      matchResults: new Map([["defect_0001", { ...matchResult("auto_bound", []), skipped: true }]]),
    }).rows[0];

    expect(row.matchState).toBe("manual");
    expect(row.matchLabel).toBe("人工选择：裂缝");
  });

  it("puts composite, candidate and unmatched rows ahead of batchable ones", () => {
    const draft = safeDraft();
    draft.defects = ["defect_0001", "defect_0002", "defect_0003"].map((candidateId, index) => ({
      ...draft.defects[0],
      candidate_id: candidateId,
      rating_tree_version_id: "tree-version-1",
      rating_tree_node_id: index === 0 ? treeNode.id : null,
      rating_tree_match_method: index === 0 ? ("exact" as const) : null,
      photo_references: [],
    }));
    const model = buildDefectPhotoReviewModel({
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      assessmentIssues: [],
      matchResults: new Map([
        ["defect_0002", matchResult("candidates", [
          { rating_tree_node_id: treeNode.id, display_name: "裂缝", match_method: "fuzzy_candidate", evidence: "相似" },
        ])],
        ["defect_0003", matchResult("composite", [])],
      ]),
    });

    expect(model.rows.map((row) => row.candidateId))
      .toEqual(["defect_0003", "defect_0002", "defect_0001"]);
  });

  it("filters by composite, candidate and unmatched issue buckets", () => {
    const draft = safeDraft();
    draft.defects = ["defect_0001", "defect_0002"].map((candidateId) => ({
      ...draft.defects[0],
      candidate_id: candidateId,
      rating_tree_version_id: "tree-version-1",
      rating_tree_node_id: null,
      rating_tree_match_method: null,
      photo_references: [],
    }));
    const base = {
      draft,
      ratingTreeVersionId: "tree-version-1",
      ratingTreeNodes: [treeNode],
      applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([treeNode.id])]]),
      treeRulesReady: true,
      assessmentIssues: [],
      matchResults: new Map([
        ["defect_0001", matchResult("composite", [])],
        ["defect_0002", matchResult("unmatched", [])],
      ]),
    };

    expect(buildDefectPhotoReviewModel({ ...base, issueFilter: "composite" as const })
      .rows.map((row) => row.candidateId)).toEqual(["defect_0001"]);
    expect(buildDefectPhotoReviewModel({ ...base, issueFilter: "unmatched" as const })
      .rows.map((row) => row.candidateId)).toEqual(["defect_0002"]);
    expect(buildDefectPhotoReviewModel(base).summary.composite).toBe(1);
    expect(buildDefectPhotoReviewModel(base).summary.unmatched).toBe(1);
  });

  // 照片问题的唯一来源是卡片：能不能入库由 ConfirmPlan 按"已确认 + 有归档文件"判定，
  // 派生的问题必须和那套判定对齐。
  it("blocks batch confirmation while a Word photo number is still unhandled", () => {
    const draft = safeDraft();
    draft.defects[0].photo_references.push({
      photo_number: "2.1-9",
      resolution: "pending",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    });

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toContain("photo_reference_pending");
    expect(row.batchEligible).toBe(false);
  });

  it("clears the problem once the missing photo is acknowledged", () => {
    const draft = safeDraft();
    draft.defects[0].photo_references.push({
      photo_number: "2.1-9",
      resolution: "missing",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    });

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems).toEqual([]);
    expect(row.batchEligible).toBe(true);
  });

  it("does not require a second confirmation for a linked archived photo", () => {
    const draft = safeDraft();

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.batchEligible).toBe(true);
  });

  it("flags a confirmed photo whose archived file is missing", () => {
    const draft = safeDraft();
    draft.photos[0] = {
      ...draft.photos[0],
      extracted_file: { ...draft.photos[0].extracted_file, archive_relative_path: null },
    };

    const row = buildDefectPhotoReviewModel({
      draft,
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.problems.map((problem) => problem.code)).toContain("photo_archive_missing");
    expect(row.batchEligible).toBe(false);
  });

  it("exposes the same card list the photo panel renders", () => {
    const row = buildDefectPhotoReviewModel({
      draft: safeDraft(),
      ...treeWiring(),
      assessmentIssues: [],
    }).rows[0];

    expect(row.photoCards.map((card) => [card.kind, card.photoNumber]))
      .toEqual([["photo", "2.1-1"]]);
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
      ...treeWiring(),
      assessmentIssues: [],
      filter: "needs_attention",
      issueFilter: "photo_pending",
      search: "2.1-1",
    });

    expect(model.rows).toHaveLength(2);
    expect(model.rows.every((row) =>
      row.problems.some((problem) => problem.code === "photo_number_conflict")
    )).toBe(true);
  });
});
