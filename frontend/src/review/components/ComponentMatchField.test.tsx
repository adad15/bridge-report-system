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

  it("renders only linked/candidate/search entries instead of the whole inventory", () => {
    const bigInventory: ComponentInventoryRevision = {
      ...inventory,
      entries: Array.from({ length: 500 }, (_, index) => ({
        ...inventory.entries[0],
        id: `entry-${index + 1}`,
        bridge_component_id: `component-${index + 1}`,
        component_number: `1-${index + 1}#`,
        mappings: [{ ...inventory.entries[0].mappings[0], id: `mapping-${index + 1}` }],
      })),
    };
    const defect = { ...data().defects[0], component_match_candidate_ids: ["component-2"] };
    const { rerender } = render(<ComponentMatchField defect={defect} inventory={bigInventory} dispatch={vi.fn()} />);

    // 默认只有占位项 + 候选，不渲染全量台账。
    expect(screen.getAllByRole("option").length).toBe(2);
    expect(screen.getByText("候选 · 1-2# / 主梁")).toBeInTheDocument();

    fireEvent.change(screen.getByLabelText("搜索实际构件"), { target: { value: "1-49" } });
    // 匹配 1-49#、1-490#…1-499#，仍在上限之内；候选保留。
    const options = screen.getAllByRole("option").map((option) => option.textContent);
    expect(options).toContain("1-49# / 主梁");
    expect(options.length).toBeLessThanOrEqual(22);

    // 无候选的病害搜索不到任何构件时，显示"没有匹配"占位。
    rerender(
      <ComponentMatchField
        defect={{ ...data().defects[0], component_match_candidate_ids: [] }}
        inventory={bigInventory}
        dispatch={vi.fn()}
      />
    );
    fireEvent.change(screen.getByLabelText("搜索实际构件"), { target: { value: "不存在" } });
    expect(screen.getByText("没有匹配的构件")).toBeInTheDocument();
  });
});
