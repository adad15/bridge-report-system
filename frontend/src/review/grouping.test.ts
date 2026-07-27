import { describe, expect, it } from "vitest";

import type { AssessmentIssue } from "../api/assessmentApi";
import type { ComponentBindingOverview } from "../api/importBindingApi";
import {
  assessmentIssueToAttention,
  buildStatistics,
  isNormalDefect,
  isNormalPhoto,
  mergeAttentionItems,
  needsAttention,
  type AttentionItem,
} from "./grouping";
import { data } from "./testFixtures";

function completeData() {
  const state = data();
  state.defects[0] = {
    ...state.defects[0],
    bridge_component_id: "component-1",
    standard_component_category_id: "category-1",
    resolved_structure_part: "上部结构",
    component_inventory_revision_id: "revision-1",
    review_status: "待确认",
  };
  state.photos[0] = { ...state.photos[0], match_status: "高置信候选", review_status: "待确认" };
  return state;
}

describe("grouping", () => {
  it("routes a system assessment component-number issue to the defect field", () => {
    expect(assessmentIssueToAttention({
      code: "component_number_missing",
      message: "构件编号不能为空",
      entity_type: "defect",
      entity_id: "defect_0001",
      field_path: "component_number",
      rule_id: "assessment.component-number-required",
    })).toMatchObject({ kind: "defect", candidateId: "defect_0001", targetField: "component_number" });
  });

  it("requires actual component category, number, location, type and description for a normal defect", () => {
    const state = completeData();
    expect(isNormalDefect(state.defects[0], state)).toBe(true);
    expect(isNormalDefect({ ...state.defects[0], component_number: "" }, state)).toBe(false);
    expect(isNormalDefect({ ...state.defects[0], standard_component_category_id: null }, state)).toBe(false);
    expect(isNormalDefect({ ...state.defects[0], defect_location: "" }, state)).toBe(false);
  });

  it("accepts only a linked high-confidence pending photo as normal", () => {
    const state = completeData();
    expect(isNormalPhoto(state.photos[0])).toBe(true);
    expect(isNormalPhoto({ ...state.photos[0], linked_defect_candidate_id: null })).toBe(false);
  });

  it("reports an unstructured measurement hint once", () => {
    const state = completeData();
    state.defects[0].measurement_text = "0.5~4.0m";
    state.defects[0].measurements = [];
    const items = needsAttention(state).filter((item) => item.warningCode === "measurement_parse_low_confidence");
    expect(items).toHaveLength(1);
    expect(items[0]).toMatchObject({ kind: "defect", targetField: "measurement_text" });
  });

  it("does not warn when a referenced missing photo was explicitly acknowledged", () => {
    const state = completeData();
    state.photos = [];
    state.defects[0].photo_references = [{
      photo_number: "2.1-1",
      resolution: "missing",
      photo_candidate_id: null,
      resolved_defect_candidate_id: null,
      review_note: null,
    }];
    expect(needsAttention(state).some((item) => item.warningCode === "photo_number_unmatched")).toBe(false);
  });

  it("uses the binding overview to hide stale match warnings and restores them after clearing", () => {
    const state = completeData();
    state.defects[0].bridge_component_id = null;
    state.defects[0].warnings = [{
      code: "defect_component_match_required",
      message: "未找到可唯一关联的实际构件，请人工选择。",
      severity: "warning",
      target_candidate_id: state.defects[0].candidate_id,
    }];
    const overview: ComponentBindingOverview = {
      inventory_confirmed: true,
      groups: [{
        part_name: state.defects[0].component_name,
        total: 1,
        bound: 1,
        unmatched: 0,
        ambiguous: 0,
        missing: 0,
        rows: [{
          component_number: state.defects[0].component_number ?? "",
          defect_count: 1,
          status: "bound",
          bridge_component_id: "component-1",
          candidate_component_ids: [],
        }],
      }],
    };

    expect(needsAttention(state, overview).some(
      (item) => item.warningCode === "defect_component_match_required"
    )).toBe(false);

    overview.groups[0].rows[0].status = "unmatched";
    overview.groups[0].rows[0].bridge_component_id = null;
    overview.groups[0].bound = 0;
    overview.groups[0].unmatched = 1;
    expect(needsAttention(state, overview).some(
      (item) => item.warningCode === "defect_component_match_required"
    )).toBe(true);
  });

  it("suppresses only the parser scale warning matched by an assessment scale error", () => {
    const parserWarnings: AttentionItem[] = [
      {
        kind: "defect",
        candidateId: "defect-1",
        message: "标度不是正整数",
        severity: "warning",
        warningCode: "defect_scale_invalid",
        targetField: "defect_scale",
      },
      {
        kind: "defect",
        candidateId: "defect-2",
        message: "标度不是正整数",
        severity: "warning",
        warningCode: "defect_scale_invalid",
        targetField: "defect_scale",
      },
      {
        kind: "defect",
        candidateId: "defect-1",
        message: "同一字段上的其他独立提示",
        severity: "warning",
        warningCode: "another_scale_warning",
        targetField: "defect_scale",
      },
    ];
    const assessmentIssues: AssessmentIssue[] = [{
      code: "assessment_defect_scale_required",
      message: "病害缺少有效的规范标度。",
      entity_type: "defect",
      entity_id: "defect-1",
      field_path: "defect_scale",
      rule_id: "",
    }];

    expect(mergeAttentionItems(parserWarnings, assessmentIssues).map(
      (item) => [item.candidateId, item.warningCode, item.severity]
    )).toEqual([
      ["defect-2", "defect_scale_invalid", "warning"],
      ["defect-1", "another_scale_warning", "warning"],
      ["defect-1", "assessment_defect_scale_required", "error"],
    ]);
    expect(mergeAttentionItems([parserWarnings[0]], [])).toEqual([parserWarnings[0]]);
  });

  it("uses the split-review warning instead of the generic pending-group warning", () => {
    const state = completeData();
    state.defects[0].warnings = [{
      code: "component_range_split_review_required",
      message: "该病害由构件范围拆分，请人工核对构件、病害和照片关联。",
      severity: "warning",
      target_candidate_id: state.defects[0].candidate_id,
    }];

    const splitItems = needsAttention(state);
    expect(splitItems.some(
      (item) => item.warningCode === "component_range_split_review_required"
    )).toBe(true);
    expect(splitItems.some((item) => item.warningCode === "defect_group_pending")).toBe(false);

    state.defects[0].warnings = [];
    expect(needsAttention(state).some(
      (item) => item.warningCode === "defect_group_pending"
    )).toBe(true);
  });

  it("counts only review candidates and never counts imported rating rows", () => {
    const state = completeData();
    const counts = buildStatistics(state);
    expect(counts).toMatchObject({
      defect_count: 1,
      photo_count: 1,
      rating_item_count: 0,
      pending_count: 2,
    });
  });
});
