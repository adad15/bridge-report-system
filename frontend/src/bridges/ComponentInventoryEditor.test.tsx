import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import {
  confirmPendingComponentInventoryMappings,
  fetchLatestComponentInventory,
  setComponentInventoryMapping,
  type ComponentInventoryRevision,
} from "../api/componentInventoryApi";
import { fetchStandardPackages } from "../api/standardsApi";
import {
  ComponentInventoryEditor,
  inventoryConfirmationBlockers,
  inventoryGroupSummaries,
} from "./ComponentInventoryEditor";

vi.mock("../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentInventoryApi")>();
  return {
    ...original,
    confirmPendingComponentInventoryMappings: vi.fn(),
    fetchLatestComponentInventory: vi.fn(),
    setComponentInventoryMapping: vi.fn(),
  };
});

vi.mock("../api/standardsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardsApi")>();
  return { ...original, fetchStandardPackages: vi.fn() };
});

const revision: ComponentInventoryRevision = {
  id: "revision-1", bridge_id: "bridge-1", revision_number: 1, status: "草稿",
  baseline_revision_id: null, confirmed_at: null,
  entries: [{
    id: "entry-1", bridge_component_id: "internal-component-id", component_number: "1-1#",
    site_name: "主梁", site_component_type: "主梁", span_or_location: "第1跨", is_active: true,
    deactivated_at: null, deactivation_reason: null, sort_order: 1, remarks: null, is_referenced: true,
    mappings: [{
      id: "mapping-1", standard_package_id: "package-1", standard_bridge_type_id: "beam",
      standard_component_category_id: "girder", structure_part: "superstructure",
      mapping_source: "自动生成", confirmation_status: "待确认", is_active: true,
    }],
  }],
};

describe("ComponentInventoryEditor", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchLatestComponentInventory).mockResolvedValue(revision);
    vi.mocked(fetchStandardPackages).mockResolvedValue([]);
    vi.mocked(setComponentInventoryMapping).mockResolvedValue({
      ...revision,
      entries: [{ ...revision.entries[0], mappings: [{ ...revision.entries[0].mappings[0], confirmation_status: "已确认" }] }],
    });
  });

  it("keeps internal component ids hidden and referenced entries deactivate-only", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    const numberInput = await screen.findByLabelText("构件编号 1-1#");
    const row = numberInput.closest("tr") as HTMLElement;
    expect(screen.queryByText("internal-component-id")).not.toBeInTheDocument();
    expect(within(row).queryByRole("button", { name: "删除" })).not.toBeInTheDocument();
    expect(within(row).getByRole("button", { name: "停用" })).toBeDisabled();
    await userEvent.type(within(row).getByLabelText("停用原因 1-1#"), "构件已拆换");
    expect(within(row).getByRole("button", { name: "停用" })).toBeEnabled();
  });

  it("centralizes unresolved mappings and confirms an existing generated mapping", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    expect(await screen.findByText(/确认前还需处理 1 项/)).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "确认映射" }));
    expect(setComponentInventoryMapping).toHaveBeenCalledWith(expect.any(String), "revision-1", "entry-1", expect.objectContaining({
      standard_component_category_id: "girder",
      mapping_source: "用户确认",
    }));
    expect(await screen.findByText(/规范映射均已确认/)).toBeInTheDocument();
  });

  it("confirms pending mappings by group and in one click", async () => {
    const confirmedRevision = {
      ...revision,
      entries: [{
        ...revision.entries[0],
        mappings: [{ ...revision.entries[0].mappings[0], confirmation_status: "已确认" }],
      }],
    };
    vi.mocked(confirmPendingComponentInventoryMappings).mockResolvedValue(confirmedRevision);
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);

    expect(await screen.findByText("分组核对")).toBeInTheDocument();
    expect(screen.getByText(/1 个构件的规范映射待确认/)).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "确认该组映射" }));
    expect(confirmPendingComponentInventoryMappings).toHaveBeenCalledWith(
      expect.any(String), "revision-1", "主梁");
    expect(await screen.findByText(/规范映射均已确认/)).toBeInTheDocument();

    vi.mocked(fetchLatestComponentInventory).mockResolvedValue(revision);
    vi.mocked(confirmPendingComponentInventoryMappings).mockClear();
    vi.mocked(confirmPendingComponentInventoryMappings).mockResolvedValue(confirmedRevision);
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await userEvent.click(await screen.findByRole("button", { name: "一键确认全部待确认映射" }));
    expect(confirmPendingComponentInventoryMappings).toHaveBeenCalledWith(
      expect.any(String), "revision-1", undefined);
  });

  it("summarizes generated groups for checking", () => {
    const summaries = inventoryGroupSummaries({
      ...revision,
      entries: [
        revision.entries[0],
        { ...revision.entries[0], id: "entry-2", component_number: "2-1#" },
        {
          ...revision.entries[0], id: "entry-3", component_number: "P1",
          site_component_type: "桥墩", site_name: "桥墩", mappings: [],
        },
      ],
    }, []);
    expect(summaries).toEqual([
      expect.objectContaining({
        siteComponentType: "主梁", activeCount: 2, firstNumber: "1-1#", lastNumber: "2-1#",
        pendingCount: 2, confirmedCount: 0, unmappedCount: 0,
        mappingLabel: "技术评定规范 · girder",
      }),
      expect.objectContaining({
        siteComponentType: "桥墩", activeCount: 1, firstNumber: "P1", lastNumber: "P1",
        pendingCount: 0, unmappedCount: 1, mappingLabel: "",
      }),
    ]);
  });

  it("reports duplicate numbers before confirmation", () => {
    const blockers = inventoryConfirmationBlockers({
      ...revision,
      entries: [
        { ...revision.entries[0], is_referenced: false, mappings: [{ ...revision.entries[0].mappings[0], confirmation_status: "已确认" }] },
        { ...revision.entries[0], id: "entry-2", bridge_component_id: "component-2", mappings: [{ ...revision.entries[0].mappings[0], id: "mapping-2", confirmation_status: "已确认" }] },
      ],
    });
    expect(blockers).toEqual(expect.arrayContaining([expect.objectContaining({ code: "duplicate_component_number", entity_id: "entry-2" })]));
  });
});
