import { describe, expect, it } from "vitest";

import type { AssessmentPreviewResponse, ConfirmedAssessmentResponse } from "../api/assessmentApi";
import {
  assessmentReducer,
  confirmedAssessmentReducer,
  currentAssessmentIssues,
  initialAssessmentState,
  initialConfirmedAssessmentState,
} from "./assessmentState";

function response(revision: number): AssessmentPreviewResponse {
  return {
    client_revision: revision,
    input_checksum: `sha256:${"a".repeat(64)}`,
    input_summary: {},
    standard: { standard_id: "H21", standard_code: "H21", standard_name: "标准", official_edition: "2011", package_version: "1.0.1", content_checksum: `sha256:${"b".repeat(64)}`, algorithm_id: "h21" },
    result: null,
    issues: [],
    assessment_run_id: null,
  };
}

describe("assessmentState", () => {
  it("does not allow an older response to overwrite a newer draft request", () => {
    let state = assessmentReducer(initialAssessmentState, { type: "requested", revision: 4 });
    state = assessmentReducer(state, { type: "requested", revision: 5 });
    state = assessmentReducer(state, { type: "resolved", response: response(4), currentRevision: 5 });
    expect(state.phase).toBe("updating");
    expect(state.response).toBeNull();

    state = assessmentReducer(state, { type: "resolved", response: response(5), currentRevision: 5 });
    expect(state.phase).toBe("blocked");
    expect(state.response?.client_revision).toBe(5);
  });

  it("keeps the last result visible while a newer request is updating", () => {
    let state = assessmentReducer(initialAssessmentState, { type: "requested", revision: 1 });
    state = assessmentReducer(state, { type: "resolved", response: response(1), currentRevision: 1 });
    state = assessmentReducer(state, { type: "requested", revision: 2 });
    expect(state.phase).toBe("updating");
    expect(state.response?.client_revision).toBe(1);
  });

  it("does not let issues from an older draft revision block current editing", () => {
    const stale = response(1);
    stale.issues = [{
      code: "assessment_defect_scale_required",
      message: "病害缺少有效的规范标度。",
      entity_type: "defect",
      entity_id: "defect-1",
      field_path: "defect_scale",
      rule_id: "scale-required",
    }];
    const state = assessmentReducer(initialAssessmentState, {
      type: "resolved",
      response: stale,
      currentRevision: 1,
    });

    expect(currentAssessmentIssues(state, 1)).toHaveLength(1);
    expect(currentAssessmentIssues(state, 2)).toEqual([]);
    expect(state.response?.issues).toHaveLength(1);
  });
});

function confirmedReport(): ConfirmedAssessmentResponse {
  return {
    ...response(0),
    result: null,
    assessment_run_id: "run-1",
    formal_revision_number: 1,
    is_current: true,
    confirmed_at: "2026-08-25T02:46:10Z",
    inspection_year: 2024,
    inspection_year_version: 1,
    inspection_year_is_current: true,
  };
}

describe("confirmedAssessmentReducer", () => {
  // 重取的那几百毫秒里界面不该先把分数清空再填回去。
  it("keeps the previous report on screen while reloading", () => {
    const loaded = confirmedAssessmentReducer(
      initialConfirmedAssessmentState, { type: "loaded", report: confirmedReport() });
    const reloading = confirmedAssessmentReducer(loaded, { type: "loading" });

    expect(reloading.phase).toBe("loading");
    expect(reloading.report?.assessment_run_id).toBe("run-1");
  });

  it("drops a stale report when the reload fails and clears on reset", () => {
    const loaded = confirmedAssessmentReducer(
      initialConfirmedAssessmentState, { type: "loaded", report: confirmedReport() });

    const failed = confirmedAssessmentReducer(loaded, { type: "failed", message: "本记录没有已入库的评定结果。" });
    expect(failed.phase).toBe("error");
    expect(failed.report).toBeNull();
    expect(failed.error).toBe("本记录没有已入库的评定结果。");

    expect(confirmedAssessmentReducer(failed, { type: "reset" })).toEqual(initialConfirmedAssessmentState);
  });
});
