import { describe, expect, it } from "vitest";

import type { ComponentBindingOverview } from "../api/importBindingApi";
import { assessmentIssueToAttention, buildStatistics, isNormalDefect, isNormalPhoto, needsAttention } from "./grouping";
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
    state.defects[0].confirmed_missing_photo_numbers = ["2.1-1"];
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
