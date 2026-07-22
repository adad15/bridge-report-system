import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";

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
  it("displays the bound component read-only", () => {
    const defect = {
      ...data().defects[0],
      bridge_component_id: "component-1",
      component_match_method: "manual" as const,
    };
    render(<ComponentMatchField defect={defect} inventory={inventory} />);
    expect(screen.getByText("2-1# / 主梁")).toBeInTheDocument();
    expect(screen.getByText("已在绑定界面人工绑定")).toBeInTheDocument();
    // 只读：不再有搜索框或下拉选择器。
    expect(screen.queryByLabelText("搜索实际构件")).not.toBeInTheDocument();
    expect(screen.queryByRole("combobox")).not.toBeInTheDocument();
  });

  it("prompts to use the binding workspace when unbound", () => {
    render(<ComponentMatchField defect={data().defects[0]} inventory={inventory} />);
    expect(screen.getByText("未绑定")).toBeInTheDocument();
    expect(screen.getByText("尚未绑定，请在构件绑定界面处理")).toBeInTheDocument();
  });

  it("shows the marked-missing state", () => {
    const defect = { ...data().defects[0], bridge_component_id: null, component_match_method: "missing" as const };
    render(<ComponentMatchField defect={defect} inventory={inventory} />);
    expect(screen.getByText("已标记缺失")).toBeInTheDocument();
    expect(screen.getByText("已在绑定界面标记缺失")).toBeInTheDocument();
  });
});
