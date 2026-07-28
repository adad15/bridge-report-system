import { describe, expect, it } from "vitest";

import { createReviewDraftReducer } from "./reviewDraft";
import { data } from "./testFixtures";

function matchedData() {
  const state = data();
  state.defects[0] = {
    ...state.defects[0],
    bridge_component_id: "component-1",
    standard_component_category_id: "category-1",
    resolved_structure_part: "上部结构",
    component_inventory_revision_id: "revision-1",
    standard_defect_indicator_id: "h21.defect.crack",
    photo_references: [{
      photo_number: "2.1-1",
      resolution: "matched",
      photo_candidate_id: "photo_0001",
      resolved_defect_candidate_id: "defect_0001",
      review_note: null,
    }],
  };
  return state;
}

describe("reviewDraftReducer", () => {
  it("adds a complete manual defect without an imported rating projection", () => {
    const reducer = createReviewDraftReducer(() => "manual_defect_0001");
    const state = matchedData();
    const next = reducer(state, {
      type: "add_defect",
      input: {
        componentName: "主梁",
        componentNumber: "2-2#梁",
        bridgeComponentId: "component-2",
        standardComponentCategoryId: "category-1",
        resolvedStructurePart: "上部结构",
        inventoryRevisionId: "revision-1",
        defectLocation: "第二跨梁底",
        defectType: "裂缝",
        ratingTreeVersionId: "tree-version-1",
        ratingTreeNodeId: "tree-node-crack",
        defectDescription: "纵向裂缝",
        defectScale: 2,
        isScoring: true,
      },
    });

    expect(next).not.toBe(state);
    expect(next.defects).toHaveLength(2);
    expect(next.defects[1]).toMatchObject({
      candidate_id: "manual_defect_0001",
      component_number: "2-2#梁",
      source_structure_part: null,
      source_ref: { source_type: "manual" },
      rating_tree_version_id: "tree-version-1",
      rating_tree_node_id: "tree-node-crack",
      rating_tree_match_method: "manual",
      standard_defect_indicator_id: null,
      review_status: "已修改",
    });
    expect(next).not.toHaveProperty("ratings");
  });

  it("deletes a defect and releases its photos for review", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    const next = reducer(state, { type: "delete_defect", candidateId: "defect_0001" });

    expect(next.defects).toEqual([]);
    expect(next.photos[0]).toMatchObject({
      linked_defect_candidate_id: null,
      match_status: "未关联",
      review_status: "已修改",
    });
    expect(state.defects).toHaveLength(1);
  });

  it("links an imported defect to an actual component and clears match warnings", () => {
    const reducer = createReviewDraftReducer();
    const state = data();
    state.defects[0].warnings = [{
      code: "defect_component_match_required",
      message: "请选择构件",
      severity: "warning",
    }];
    const next = reducer(state, {
      type: "link_defect_component",
      candidateId: "defect_0001",
      component: {
        componentName: "主梁",
        componentNumber: "2-1#梁",
        bridgeComponentId: "component-1",
        standardComponentCategoryId: "category-1",
        resolvedStructurePart: "上部结构",
        inventoryRevisionId: "revision-1",
      },
    });

    expect(next.defects[0]).toMatchObject({
      bridge_component_id: "component-1",
      standard_component_category_id: "category-1",
      component_match_method: "manual",
      review_status: "已修改",
    });
    expect(next.defects[0].warnings).toEqual([]);
  });

  it("re-parses a measurement range and marks the group pending", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].group_review_status = "已确认";
    const next = reducer(state, {
      type: "edit_measurement_text",
      candidateId: "defect_0001",
      text: "0.5~4.0m",
    });

    expect(next.defects[0].measurements[0]).toMatchObject({
      value_type: "range",
      minimum_value: 0.5,
      maximum_value: 4,
      unit: "m",
    });
    expect(next.defects[0].group_review_status).toBe("待确认");
  });

  it("relinks a photo and invalidates both affected groups", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].group_review_status = "已确认";
    state.defects.push({ ...state.defects[0], candidate_id: "defect_0002", group_review_status: "已确认" });
    const next = reducer(state, {
      type: "photo_relink",
      candidateId: "photo_0001",
      defectCandidateId: "defect_0002",
    });

    expect(next.photos[0]).toMatchObject({ linked_defect_candidate_id: "defect_0002", review_status: "已修改" });
    expect(next.defects.map((item) => item.group_review_status)).toEqual(["待确认", "待确认"]);
  });

  it("records an explicit missing-photo acknowledgement", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].photo_references = [{
      photo_number: "missing-1",
      resolution: "pending",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    }];
    state.photos = [];
    const next = reducer(state, {
      type: "confirm_missing_photo",
      defectCandidateId: "defect_0001",
      photoNumber: "missing-1",
    });
    expect(next.defects[0].photo_references[0].resolution).toBe("missing");
  });

  it("confirms a complete defect-photo group", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].review_status = "待确认";
    const next = reducer(state, { type: "confirm_defect_group", defectCandidateId: "defect_0001" });
    expect(next.defects[0]).toMatchObject({ group_review_status: "已确认", review_status: "已确认" });
  });

  it("clears only the temporary range-split warning when confirming the group", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].warnings = [
      { code: "component_range_split_review_required", message: "待核对", severity: "warning", target_candidate_id: "defect_0001" },
      { code: "another_warning", message: "保留", severity: "warning", target_candidate_id: "defect_0001" },
    ];
    state.defects[0].range_split_origin = {
      operation_id: "op-1",
      source_candidate_id: "source-1",
      source_component_number: "1-1#梁~1-3#梁",
      expanded_component_number: "1-1#梁",
      split_index: 1,
      split_count: 3,
      operated_by_user_id: "user-1",
      operated_at: "2026-07-24T08:00:00Z",
    };
    const next = reducer(state, {
      type: "confirm_defect_group",
      defectCandidateId: "defect_0001",
    });
    expect(next.defects[0].warnings.map((warning) => warning.code)).toEqual(["another_warning"]);
    expect(next.defects[0].range_split_origin).toEqual(state.defects[0].range_split_origin);
  });

  it("returns the same state when deleting an unknown defect", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    expect(reducer(state, { type: "delete_defect", candidateId: "missing" })).toBe(state);
  });

  it("selects a standard indicator and derives the modified state", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].review_status = "已确认";
    const sourceRef = state.defects[0].source_ref;

    const next = reducer(state, {
      type: "select_standard_defect_indicator",
      candidateId: "defect_0001",
      indicatorId: "h21.defect.spalling",
      indicatorName: "混凝土剥落",
    });

    expect(next.defects[0]).toMatchObject({
      standard_defect_indicator_id: "h21.defect.spalling",
      defect_type: "混凝土剥落",
      review_status: "已修改",
      group_review_status: "待确认",
    });
    expect(next.defects[0].source_ref).toBe(sourceRef);
  });

  it("batch confirms only the requested groups and atomically accepts unique photo matches", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].review_status = "已修改";
    state.defects[0].photo_references[0] = {
      photo_number: "2.1-1",
      resolution: "pending",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    };
    state.photos[0] = {
      ...state.photos[0],
      photo_number: "2.1-1",
      linked_defect_candidate_id: "defect_0001",
      match_status: "高置信候选",
      review_status: "待确认",
      extracted_file: {
        ...state.photos[0].extracted_file,
        archive_relative_path: "photos/2.1-1.jpg",
      },
    };

    const next = reducer(state, {
      type: "confirm_defect_groups",
      candidateIds: ["defect_0001"],
    });

    expect(next.defects[0]).toMatchObject({
      group_review_status: "已确认",
      review_status: "已修改",
    });
    expect(next.defects[0].photo_references[0]).toMatchObject({
      resolution: "matched",
      photo_candidate_id: "photo_0001",
      resolved_defect_candidate_id: "defect_0001",
    });
    expect(next.photos[0]).toMatchObject({
      match_status: "已确认",
      review_status: "已确认",
    });
  });

  it("clears a stale tree node after component rebinding and accepts a manual tree selection", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    Object.assign(state.defects[0], {
      rating_tree_version_id: "tree-version-1",
      rating_tree_node_id: "old-node",
      rating_tree_match_method: "exact",
      standard_defect_indicator_id: "old-h21",
    });
    const rebound = reducer(state, {
      type: "link_defect_component",
      candidateId: "defect_0001",
      component: {
        componentName: "主梁",
        componentNumber: "2-1#梁",
        bridgeComponentId: "component-2",
        standardComponentCategoryId: "category-1",
        resolvedStructurePart: "上部结构",
        inventoryRevisionId: "revision-1",
      },
    });
    expect(rebound.defects[0]).toMatchObject({
      rating_tree_version_id: null,
      rating_tree_node_id: null,
      rating_tree_match_method: null,
      standard_defect_indicator_id: null,
    });

    const selected = reducer(rebound, {
      type: "select_rating_tree_node",
      candidateId: "defect_0001",
      versionId: "tree-version-1",
      nodeId: "tree-node-crack",
      nodeName: "裂缝",
      isScoring: true,
      matchEvidence: "人工选择",
    });
    expect(selected.defects[0]).toMatchObject({
      rating_tree_version_id: "tree-version-1",
      rating_tree_node_id: "tree-node-crack",
      rating_tree_match_method: "manual",
      rating_tree_match_evidence: "人工选择",
      defect_type: "裂缝",
      standard_defect_indicator_id: null,
    });
  });

  it("ignores and restores a defect through explicit actions", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    const ignored = reducer(state, { type: "ignore_defect", candidateId: "defect_0001" });
    const restored = reducer(ignored, { type: "restore_ignored_defect", candidateId: "defect_0001" });

    expect(ignored.defects[0]).toMatchObject({ review_status: "已忽略", group_review_status: "待确认" });
    expect(restored.defects[0]).toMatchObject({ review_status: "待确认", group_review_status: "待确认" });
  });

  it("records that a Word photo reference actually belongs to another defect", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].photo_references[0] = {
      photo_number: "2.1-1",
      resolution: "pending",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    };
    state.defects.push({
      ...state.defects[0],
      candidate_id: "defect_0002",
      photo_references: [],
    });

    const next = reducer(state, {
      type: "relink_photo_reference",
      defectCandidateId: "defect_0001",
      photoNumber: "2.1-1",
      photoCandidateId: "photo_0001",
      targetDefectCandidateId: "defect_0002",
    });

    expect(next.defects[0].photo_references[0]).toMatchObject({
      resolution: "relinked",
      photo_candidate_id: "photo_0001",
      resolved_defect_candidate_id: "defect_0002",
    });
    expect(next.photos[0]).toMatchObject({
      linked_defect_candidate_id: "defect_0002",
      match_status: "已确认",
    });
  });
});
