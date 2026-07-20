import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { AssessmentPreviewResponse } from "../../api/assessmentApi";
import { AssessmentSection } from "./AssessmentSection";

function successfulResponse(): AssessmentPreviewResponse {
  return {
    client_revision: 2,
    input_checksum: `sha256:${"a".repeat(64)}`,
    input_summary: {},
    standard: { standard_id: "JTG_H21_2011", standard_code: "JTG/T H21—2011", standard_name: "公路桥梁技术状况评定标准", official_edition: "2011", package_version: "1.0.1", content_checksum: `sha256:${"b".repeat(64)}`, algorithm_id: "jtg-h21-2011" },
    result: {
      standard_id: "JTG_H21_2011", package_version: "1.0.1", bridge_type_id: "beam", overall_score: 87.25, calculated_grade: 2, final_grade: 2, explanation: "系统按 H21 计算。",
      structure_parts: [{ structure_part: "superstructure", score: 85.5, grade: 2, overall_weight: 0.4, categories: [] }],
      triggered_controls: [], trace: [{ step: "component", rule_id: "rule-1", entity_id: "component-1", source_reference: "4.1.1", inputs: {}, output: {} }],
    },
    issues: [], assessment_run_id: "run-1",
  };
}

describe("AssessmentSection", () => {
  it("shows system standard identity, calculated results and trace", () => {
    render(<AssessmentSection phase="ready" response={successfulResponse()} error={null} onRetry={vi.fn()} onSelectIssue={vi.fn()} />);
    expect(screen.getByText(/JTG\/T H21—2011/)).toBeInTheDocument();
    expect(screen.getByText(/规则包 1.0.1/)).toBeInTheDocument();
    expect(screen.getByText("87.25")).toBeInTheDocument();
    expect(screen.getByText(/4.1.1/)).toBeInTheDocument();
    expect(screen.queryByText(/Word 评分/)).not.toBeInTheDocument();
  });

  it("keeps retry available for blocked or failed previews", () => {
    const retry = vi.fn();
    const blocked = successfulResponse();
    blocked.result = null;
    blocked.issues = [{ code: "assessment_defect_scale_required", message: "缺少标度", entity_type: "defect", entity_id: "defect-1", field_path: "defect_scale", rule_id: "" }];
    render(<AssessmentSection phase="blocked" response={blocked} error={null} onRetry={retry} onSelectIssue={vi.fn()} />);
    fireEvent.click(screen.getByRole("button", { name: "重新试算" }));
    expect(retry).toHaveBeenCalledTimes(1);
    expect(screen.getByText("缺少标度")).toBeInTheDocument();
  });
});
