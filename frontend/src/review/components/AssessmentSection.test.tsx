import { fireEvent, render, screen, within } from "@testing-library/react";
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
      structure_parts: [
        {
          structure_part: "superstructure", score: 85.5, grade: 2, overall_weight: 0.4,
          categories: [category("h21.component.beam.upper_bearing", "上部承重构件（主梁、挂梁）", "superstructure", 81.54, 0.7, 25)],
        },
        {
          structure_part: "substructure", score: 88.89, grade: 2, overall_weight: 0.4,
          categories: [category("h21.component.lower.pier", "桥墩", "substructure", 78.41, 0.3, 11)],
        },
        {
          structure_part: "deck_system", score: 71.51, grade: 3, overall_weight: 0.2,
          categories: [category("h21.component.deck.pavement", "桥面铺装", "deck_system", 72.21, 0.2, 8)],
        },
      ],
      triggered_controls: [], trace: [{ step: "component", rule_id: "rule-1", entity_id: "component-1", source_reference: "4.1.1", inputs: {}, output: {} }],
    },
    issues: [], assessment_run_id: "run-1",
  };
}

function category(
  componentTypeId: string,
  componentTypeName: string,
  structurePart: string,
  score: number,
  effectiveWeight: number,
  componentCount: number,
) {
  return {
    component_type_id: componentTypeId,
    component_type_name: componentTypeName,
    structure_part: structurePart,
    major: false,
    score,
    grade: score >= 80 ? 2 : 3,
    mean_component_score: score,
    minimum_component_score: score,
    configured_weight: effectiveWeight,
    effective_weight: effectiveWeight,
    low_score_passthrough: false,
    component_count_factor: null,
    components: Array.from({ length: componentCount }, (_, index) => ({
      component_instance_id: `component-${componentTypeId}-${index}`,
      component_type_id: componentTypeId,
      structure_part: structurePart,
      major: false,
      score,
      ordered_deductions: [],
      defects: [],
    })),
  };
}

function rowOf(label: string): HTMLElement {
  const row = screen.getByText(label).closest("tr");
  if (!row) throw new Error(`找不到 ${label} 所在的表格行`);
  return row;
}

describe("AssessmentSection", () => {
  it("shows system standard identity, calculated results and trace", () => {
    render(<AssessmentSection phase="ready" response={successfulResponse()} error={null} onRetry={vi.fn()} onSelectIssue={vi.fn()} />);
    expect(screen.getByText(/JTG\/T H21—2011/)).toBeInTheDocument();
    expect(screen.getByText(/规则包 1.0.1/)).toBeInTheDocument();
    expect(screen.getByText("87.25")).toBeInTheDocument();
    expect(screen.getByText("桥墩")).toBeInTheDocument();
    expect(screen.getByText("桥面铺装")).toBeInTheDocument();
    // 分部行是那一组的合计行，单类别分部下合计与类别本身同值，断言要落到具体行上。
    const superstructureRow = rowOf("上部结构");
    expect(within(superstructureRow).getByText("85.50")).toBeInTheDocument();
    expect(within(superstructureRow).getByText("0.4000")).toBeInTheDocument();
    const bearingRow = rowOf("上部承重构件");
    expect(within(bearingRow).getByText("81.54")).toBeInTheDocument();
    expect(within(bearingRow).getByText("0.7000")).toBeInTheDocument();
    expect(within(bearingRow).getByText("25")).toBeInTheDocument();
    expect(within(bearingRow).getByText("2 类")).toBeInTheDocument();
    expect(within(rowOf("桥面系")).getByText("3 类")).toBeInTheDocument();
    // 得分条按分数取宽度，等级决定填色档位。
    expect(bearingRow.querySelector(".assessment-score-bar-fill")).toHaveStyle({ width: "81.54%" });
    expect(bearingRow.querySelector(".assessment-score-bar-fill")).toHaveClass("assessment-score-bar-fill-2");
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
