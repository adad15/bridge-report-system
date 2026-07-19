import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { ComponentInventoryRevision } from "../../api/componentInventoryApi";
import { data } from "../testFixtures";
import { ComponentMatchField } from "./ComponentMatchField";

const inventory: ComponentInventoryRevision = {
  id: "revision-1",
  bridge_id: "bridge-1",
  revision_number: 1,
  status: "已确认",
  baseline_revision_id: null,
  confirmed_at: "2026-07-19",
  entries: [{
    id: "entry-1",
    bridge_component_id: "component-1",
    component_number: "2-1#",
    site_name: "1号主梁",
    site_component_type: "主梁",
    span_or_location: "第二跨",
    is_active: true,
    deactivated_at: null,
    deactivation_reason: null,
    sort_order: 1,
    remarks: null,
    is_referenced: false,
    mappings: [{
      id: "mapping-1",
      standard_package_id: "package-1",
      standard_bridge_type_id: "beam",
      standard_component_category_id: "main-girder",
      structure_part: "superstructure",
      mapping_source: "规范模板",
      confirmation_status: "已确认",
      is_active: true,
    }],
  }],
};

describe("ComponentMatchField", () => {
  it("shows candidates and derives hidden mapping fields from the selected inventory component", () => {
    const defect = {
      ...data().defects[0],
      component_match_candidate_ids: ["component-1"],
    };
    const dispatch = vi.fn();
    render(<ComponentMatchField defect={defect} inventory={inventory} dispatch={dispatch} />);

    expect(screen.getByText("候选 · 2-1# / 主梁")).toBeInTheDocument();
    fireEvent.change(screen.getByLabelText("实际构件"), { target: { value: "component-1" } });

    expect(dispatch).toHaveBeenCalledWith({
      type: "link_defect_component",
      candidateId: "defect_0001",
      component: {
        componentName: "主梁",
        componentNumber: "2-1#",
        bridgeComponentId: "component-1",
        standardComponentCategoryId: "main-girder",
        resolvedStructurePart: "上部结构",
        inventoryRevisionId: "revision-1",
      },
    });
  });

  it("warns when the available inventory is not confirmed", () => {
    render(<ComponentMatchField defect={data().defects[0]} inventory={{ ...inventory, status: "草稿" }} dispatch={vi.fn()} />);
    expect(screen.getByText(/当前台账尚未确认/)).toBeInTheDocument();
  });
});
