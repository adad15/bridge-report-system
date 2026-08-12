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

  it("confirms a complete defect-photo group", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].review_status = "待确认";
    const next = reducer(state, { type: "confirm_defect_groups", candidateIds: ["defect_0001"] });
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
      type: "confirm_defect_groups",
      candidateIds: ["defect_0001"],
    });
    expect(next.defects[0].warnings.map((warning) => warning.code)).toEqual(["another_warning"]);
    expect(next.defects[0].range_split_origin).toEqual(state.defects[0].range_split_origin);
  });

  it("returns the same state when deleting an unknown defect", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    expect(reducer(state, { type: "delete_defect", candidateId: "missing" })).toBe(state);
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
    expect(next.photos[0]).toEqual(state.photos[0]);
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

  it("applies automatic tree matches without confirming them", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    const applied = reducer(state, {
      type: "apply_rating_tree_auto_matches",
      versionId: "tree-version-1",
      matches: [{
        candidateId: "defect_0001",
        nodeId: "tree-node-water",
        matchMethod: "controlled_keyword",
        matchEvidence: "命中受控关键词“渗水”。",
        isScoring: true,
      }],
    });

    expect(applied.defects[0]).toMatchObject({
      rating_tree_version_id: "tree-version-1",
      rating_tree_node_id: "tree-node-water",
      rating_tree_match_method: "controlled_keyword",
      rating_tree_match_evidence: "命中受控关键词“渗水”。",
      group_review_status: "待确认",
    });
    expect(applied.defects[0].review_status).toBe(state.defects[0].review_status);
  });

  it("never lets an automatic tree match overwrite manual, confirmed or ignored records", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects = [
      { ...state.defects[0], candidate_id: "defect_manual", rating_tree_node_id: "picked", rating_tree_match_method: "manual" },
      { ...state.defects[0], candidate_id: "defect_confirmed", group_review_status: "已确认" },
      { ...state.defects[0], candidate_id: "defect_ignored", review_status: "已忽略" },
    ];
    const matches = state.defects.map((defect) => ({
      candidateId: defect.candidate_id,
      nodeId: "tree-node-water",
      matchMethod: "exact" as const,
      matchEvidence: "自动匹配",
      isScoring: true,
    }));

    const applied = reducer(state, {
      type: "apply_rating_tree_auto_matches",
      versionId: "tree-version-1",
      matches,
    });

    expect(applied.defects[0].rating_tree_node_id).toBe("picked");
    expect(applied.defects[0].rating_tree_match_method).toBe("manual");
    expect(applied.defects[1].group_review_status).toBe("已确认");
    expect(applied.defects[1].rating_tree_node_id).toBe(state.defects[1].rating_tree_node_id);
    expect(applied.defects[2].rating_tree_node_id).toBe(state.defects[2].rating_tree_node_id);
    expect(applied.defects[2].review_status).toBe("已忽略");
  });

  describe("照片归属与缺图操作", () => {
    function unlinkedPhoto() {
      const state = matchedData();
      state.photos[0] = {
        ...state.photos[0],
        linked_defect_candidate_id: null,
      };
      state.defects[0] = { ...state.defects[0], photo_references: [] };
      return state;
    }

    it("links a photo without creating a separate confirmation state", () => {
      const reducer = createReviewDraftReducer();
      const state = unlinkedPhoto();

      const next = reducer(state, {
        type: "link_photo_to_defect",
        photoCandidateId: "photo_0001",
        defectCandidateId: "defect_0001",
      });

      expect(next.photos[0]).toMatchObject({
        linked_defect_candidate_id: "defect_0001",
      });
      expect(next.defects[0].group_review_status).toBe("待确认");
      expect(state.photos[0].linked_defect_candidate_id).toBeNull();
    });

    it("marks the matching Word reference when the numbers line up", () => {
      const reducer = createReviewDraftReducer();
      const state = unlinkedPhoto();
      state.defects[0] = {
        ...state.defects[0],
        photo_references: [{
          photo_number: "2.1-1",
          resolution: "pending",
          photo_candidate_id: null,
          resolved_defect_candidate_id: null,
          review_note: null,
        }],
      };

      const next = reducer(state, {
        type: "link_photo_to_defect",
        photoCandidateId: "photo_0001",
        defectCandidateId: "defect_0001",
      });

      expect(next.defects[0].photo_references[0]).toMatchObject({
        resolution: "matched",
        photo_candidate_id: "photo_0001",
        resolved_defect_candidate_id: "defect_0001",
      });
    });

    it("returns the same state when the photo or defect is unknown", () => {
      const reducer = createReviewDraftReducer();
      const state = unlinkedPhoto();

      expect(reducer(state, {
        type: "link_photo_to_defect",
        photoCandidateId: "missing",
        defectCandidateId: "defect_0001",
      })).toBe(state);
      expect(reducer(state, {
        type: "link_photo_to_defect",
        photoCandidateId: "photo_0001",
        defectCandidateId: "missing",
      })).toBe(state);
    });

    it("unlinks a Word photo back to the unassigned list and reopens its reference", () => {
      const reducer = createReviewDraftReducer();
      const state = matchedData();

      const next = reducer(state, {
        type: "unlink_photo_from_defect",
        photoCandidateId: "photo_0001",
      });

      expect(next.photos[0]).toMatchObject({
        linked_defect_candidate_id: null,
      });
      expect(next.defects[0].photo_references[0]).toMatchObject({
        resolution: "pending",
        photo_candidate_id: null,
        resolved_defect_candidate_id: null,
      });
      expect(next.defects[0].group_review_status).toBe("待确认");
    });

    it("acknowledges a missing photo and takes it back", () => {
      const reducer = createReviewDraftReducer();
      const state = matchedData();
      state.defects[0] = {
        ...state.defects[0],
        photo_references: [{
          photo_number: "2.1-9",
          resolution: "pending",
          photo_candidate_id: null,
          resolved_defect_candidate_id: null,
          review_note: null,
        }],
      };

      const missing = reducer(state, {
        type: "set_photo_reference_missing",
        defectCandidateId: "defect_0001",
        photoNumber: "2.1-9",
        missing: true,
      });
      expect(missing.defects[0].photo_references[0].resolution).toBe("missing");

      const undone = reducer(missing, {
        type: "set_photo_reference_missing",
        defectCandidateId: "defect_0001",
        photoNumber: "2.1-9",
        missing: false,
      });
      expect(undone.defects[0].photo_references[0].resolution).toBe("pending");
    });

    // 这张图明明在本次导入里，只是没挂上——"原报告缺图"就是假话。
    it("refuses to call a photo missing when the import actually has that number", () => {
      const reducer = createReviewDraftReducer();
      const state = unlinkedPhoto();
      state.defects[0] = {
        ...state.defects[0],
        photo_references: [{
          photo_number: "2.1-1",
          resolution: "pending",
          photo_candidate_id: null,
          resolved_defect_candidate_id: null,
          review_note: null,
        }],
      };

      expect(reducer(state, {
        type: "set_photo_reference_missing",
        defectCandidateId: "defect_0001",
        photoNumber: "2.1-1",
        missing: true,
      })).toBe(state);
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

  // 换绑不再是原语：先从原病害摘掉，再挂到目标病害。两侧的组都要打回待确认。
  it("relinks a photo across defects in two steps", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].group_review_status = "已确认";
    state.defects.push({
      ...state.defects[0],
      candidate_id: "defect_0002",
      group_review_status: "已确认",
      photo_references: [],
    });

    const unlinked = reducer(state, {
      type: "unlink_photo_from_defect",
      photoCandidateId: "photo_0001",
    });
    expect(unlinked.photos[0].linked_defect_candidate_id).toBeNull();
    expect(unlinked.defects[0].photo_references[0].resolution).toBe("pending");

    const relinked = reducer(unlinked, {
      type: "link_photo_to_defect",
      photoCandidateId: "photo_0001",
      defectCandidateId: "defect_0002",
    });

    expect(relinked.photos[0]).toMatchObject({
      linked_defect_candidate_id: "defect_0002",
    });
    expect(relinked.defects.map((item) => item.group_review_status)).toEqual(["待确认", "待确认"]);
  });

  it("mirrors an uploaded photo into the draft and reopens its defect group", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].group_review_status = "已确认";
    const uploaded = {
      ...state.photos[0],
      candidate_id: "manual_photo_0001",
      photo_number: "补-1",
      linked_defect_candidate_id: "defect_0001",
      source_ref: { source_type: "manual" as const },
    };

    const next = reducer(state, { type: "add_photo", photo: uploaded });

    expect(next.photos).toHaveLength(2);
    expect(next.photos[1]).toEqual(uploaded);
    expect(next.defects[0].group_review_status).toBe("待确认");
    // 上传的照片没有 Word 引用条目，原有引用一个都不动。
    expect(next.defects[0].photo_references).toEqual(state.defects[0].photo_references);
  });

  it("refuses an uploaded photo whose defect is not in this import", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    const stray = {
      ...state.photos[0],
      candidate_id: "manual_photo_0001",
      linked_defect_candidate_id: "defect_9999",
    };

    expect(reducer(state, { type: "add_photo", photo: stray })).toBe(state);
  });

  it("drops a removed upload without touching the remaining photos", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects[0].group_review_status = "已确认";
    const uploaded = {
      ...state.photos[0],
      candidate_id: "manual_photo_0001",
      photo_number: "补-1",
      linked_defect_candidate_id: "defect_0001",
      source_ref: { source_type: "manual" as const },
    };
    const withUpload = reducer(state, { type: "add_photo", photo: uploaded });

    const next = reducer(withUpload, { type: "remove_photo", photoCandidateId: "manual_photo_0001" });

    expect(next.photos.map((item) => item.candidate_id)).toEqual(["photo_0001"]);
    expect(next.defects[0].group_review_status).toBe("待确认");
  });

  it("assigns one rating tree node to a selected source group without touching other records", () => {
    const reducer = createReviewDraftReducer();
    const state = matchedData();
    state.defects = [
      {
        ...state.defects[0],
        candidate_id: "defect_0001",
        rating_tree_node_id: null,
        rating_tree_match_method: null,
        source_defect_group_id: "group-a",
        source_defect_indicator_id: "indicator-a",
      },
      {
        ...state.defects[0],
        candidate_id: "defect_0002",
        rating_tree_node_id: null,
        rating_tree_match_method: null,
        source_defect_group_id: "group-a",
        source_defect_indicator_id: "indicator-a",
      },
      {
        ...state.defects[0],
        candidate_id: "defect_0003",
        rating_tree_node_id: null,
        rating_tree_match_method: null,
        source_defect_group_id: "group-b",
        source_defect_indicator_id: "indicator-b",
      },
    ];

    const next = reducer(state, {
      type: "select_rating_tree_nodes",
      candidateIds: ["defect_0001", "defect_0002"],
      versionId: "tree-version-2",
      nodeId: "tree-node-other",
      nodeName: "其他病害",
      isScoring: false,
      matchEvidence: "用户按相同来源身份批量指定评定树病害",
    });

    expect(next.defects.slice(0, 2)).toEqual(expect.arrayContaining([
      expect.objectContaining({
        rating_tree_version_id: "tree-version-2",
        rating_tree_node_id: "tree-node-other",
        rating_tree_match_method: "manual",
        rating_tree_match_evidence: "用户按相同来源身份批量指定评定树病害",
        defect_type: "其他病害",
        defect_scale: null,
        group_review_status: "待确认",
      }),
      expect.objectContaining({
        rating_tree_version_id: "tree-version-2",
        rating_tree_node_id: "tree-node-other",
      }),
    ]));
    expect(next.defects[0].source_defect_group_id).toBe("group-a");
    expect(next.defects[2]).toBe(state.defects[2]);
  });
});
