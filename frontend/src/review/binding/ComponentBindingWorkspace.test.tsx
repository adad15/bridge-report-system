import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchLatestComponentInventory } from "../../api/componentInventoryApi";
import {
  bindComponent,
  fetchComponentBinding,
  markComponentMissing,
} from "../../api/importBindingApi";
import { ComponentBindingWorkspace } from "./ComponentBindingWorkspace";

vi.mock("../../api/importBindingApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../../api/importBindingApi")>();
  return {
    ...original,
    fetchComponentBinding: vi.fn(),
    bindComponent: vi.fn(),
    markComponentMissing: vi.fn(),
    clearComponentBinding: vi.fn(),
  };
});

vi.mock("../../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../../api/componentInventoryApi")>();
  return { ...original, fetchLatestComponentInventory: vi.fn() };
});

const inventory = {
  id: "rev-1",
  bridge_id: "bridge-1",
  revision_number: 1,
  status: "已确认" as const,
  baseline_revision_id: null,
  confirmed_at: null,
  entries: [
    {
      id: "entry-1",
      bridge_component_id: "c1",
      component_number: "1-1#梁",
      site_name: "空心板",
      site_component_type: "空心板",
      span_or_location: null,
      is_active: true,
      deactivated_at: null,
      deactivation_reason: null,
      sort_order: 1,
      remarks: null,
      is_referenced: false,
      mappings: [
        {
          id: "m1",
          standard_package_id: "p1",
          standard_bridge_type_id: "h21.bridge_type.beam",
          standard_component_category_id: "h21.component.beam.upper_bearing",
          structure_part: "superstructure" as const,
          mapping_source: "规范模板",
          confirmation_status: "已确认",
          is_active: true,
        },
      ],
    },
  ],
};

function overview(status: "unmatched" | "bound" | "missing") {
  return {
    inventory_confirmed: true,
    groups: [
      {
        part_name: "上部承重构件",
        total: 1,
        bound: status === "bound" ? 1 : 0,
        unmatched: status === "unmatched" ? 1 : 0,
        ambiguous: 0,
        missing: status === "missing" ? 1 : 0,
        rows: [
          {
            component_number: "1-1#梁",
            defect_count: 3,
            status,
            bridge_component_id: status === "bound" ? "c1" : null,
            candidate_component_ids: status === "unmatched" ? ["c1"] : [],
          },
        ],
      },
    ],
  };
}

describe("ComponentBindingWorkspace", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchLatestComponentInventory).mockResolvedValue(inventory);
    vi.mocked(fetchComponentBinding).mockResolvedValue(overview("unmatched"));
  });

  it("renders grouped rows with reference counts", async () => {
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" />);
    expect(await screen.findByText("上部承重构件")).toBeInTheDocument();
    expect(screen.getByText("引用 3 条")).toBeInTheDocument();
    expect(screen.getByText("已处理 0 / 共 1")).toBeInTheDocument();
  });

  it("binds a row to the selected component and reflects the new status", async () => {
    vi.mocked(bindComponent).mockResolvedValue(overview("bound"));
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" />);

    await userEvent.selectOptions(
      await screen.findByLabelText("为 1-1#梁 选择实际构件"), "c1");

    await waitFor(() => expect(bindComponent).toHaveBeenCalledWith("http://127.0.0.1:18080", "i1", {
      part_name: "上部承重构件",
      component_number: "1-1#梁",
      bridge_component_id: "c1",
    }));
    expect(await screen.findByText("已绑定")).toBeInTheDocument();
  });

  it("marks a row missing and enables entering review when all resolved", async () => {
    vi.mocked(markComponentMissing).mockResolvedValue(overview("missing"));
    const onEnterReview = vi.fn();
    render(<ComponentBindingWorkspace importId="i1" bridgeId="bridge-1" onEnterReview={onEnterReview} />);

    await userEvent.click(await screen.findByLabelText("标记缺失 1-1#梁"));

    await waitFor(() => expect(markComponentMissing).toHaveBeenCalled());
    const enter = await screen.findByRole("button", { name: "全部绑定完成，进入校对" });
    expect(enter).toBeEnabled();
    await userEvent.click(enter);
    expect(onEnterReview).toHaveBeenCalled();
  });
});
