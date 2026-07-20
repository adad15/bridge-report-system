import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchStandardCatalog, fetchStandardPackages } from "../api/standardsApi";
import { BridgeInventoryWizard, previewInventoryNumbers } from "./BridgeInventoryWizard";

vi.mock("../api/standardsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardsApi")>();
  return { ...original, fetchStandardCatalog: vi.fn(), fetchStandardPackages: vi.fn() };
});

const packageSummary = {
  id: "package-1", family: "technical_condition" as const, standard_id: "H21",
  standard_code: "JTG/T H21—2011", standard_name: "公路桥梁技术状况评定标准",
  official_edition: "2011", package_version: "1.0.0", contract_version: 1,
  algorithm_id: "h21", effective_date: "2011-09-01", content_checksum: "sha256:test",
  is_enabled: true, sync_status: "正常" as const, sync_error_code: null, sync_error_message: null,
};

const catalog = {
  package: packageSummary,
  bridge_types: [
    { id: "beam", code: "beam", name: "梁式桥" },
    { id: "arch", code: "arch", name: "拱桥" },
  ],
  component_categories: [
    { id: "girder", name: "主梁", bridge_type_ids: ["beam"], structure_part: "superstructure" as const, generatable: true },
    { id: "arch-ring", name: "主拱圈", bridge_type_ids: ["arch"], structure_part: "superstructure" as const, generatable: true },
    { id: "pavement", name: "桥面铺装", bridge_type_ids: ["beam", "arch"], structure_part: "deck_system" as const, generatable: true },
    { id: "riverbed", name: "河床", bridge_type_ids: ["beam", "arch"], structure_part: "substructure" as const, generatable: false },
  ],
  inventory_templates: [
    { id: "beam-template", references: ["girder"], bridge_type_id: "beam", quantity_inputs: ["span_count", "upper_bearing_members_per_span"], numbering_is_user_editable: true },
    { id: "arch-template", references: ["arch-ring"], bridge_type_id: "arch", quantity_inputs: ["span_count", "main_arch_ring_count"], numbering_is_user_editable: true },
  ],
  defect_catalogs: [], maintenance_levels: [], inspection_types: [], periodic_inspection_requirements: [],
};

describe("BridgeInventoryWizard", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchStandardPackages).mockResolvedValue([packageSummary]);
    vi.mocked(fetchStandardCatalog).mockResolvedValue(catalog);
  });

  it("explains that the template source does not bind future scoring standards", async () => {
    render(<BridgeInventoryWizard onPlanChange={vi.fn()} />);
    expect(await screen.findByText(/不会绑定或限制以后检测项目采用的评分规范/)).toBeInTheDocument();
    expect(screen.queryByText("beam-template")).not.toBeInTheDocument();
    expect(screen.queryByText("girder")).not.toBeInTheDocument();
  });

  it("clears incompatible quantities when the bridge type changes", async () => {
    render(<BridgeInventoryWizard onPlanChange={vi.fn()} />);
    const bridgeType = await screen.findByLabelText("桥型");
    await userEvent.selectOptions(bridgeType, "beam");
    const spanCount = screen.getByLabelText("跨数");
    await userEvent.type(spanCount, "5");
    expect(spanCount).toHaveValue(5);
    await userEvent.selectOptions(bridgeType, "arch");
    expect(screen.getByLabelText("跨数")).toHaveValue(null);
    expect(screen.queryByLabelText("每跨上部承重构件数")).not.toBeInTheDocument();
  });

  it("builds the same span-member number preview as the backend", () => {
    expect(previewInventoryNumbers([{
      site_component_type: "主梁", site_name: "主梁", standard_component_category_id: "girder",
      structure_part: "superstructure", numbering_mode: "span_member", quantity: 2,
      quantity_key: "girders", number_prefix: "G", number_suffix: "#",
    }], 2)).toEqual([{
      quantityKey: "girders", siteType: "主梁", count: 4,
      numbers: ["G1-1#", "G1-2#", "G2-1#", "G2-2#"],
    }]);
  });

  it("emits a complete generation plan after category and quantities are filled", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "beam");
    await userEvent.type(screen.getByLabelText("跨数"), "2");
    await userEvent.type(screen.getByLabelText("每跨上部承重构件数"), "3");
    await userEvent.selectOptions(screen.getByLabelText("对应构件类别"), "girder");

    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      template_id: "beam-template",
      span_count: 2,
      input_quantities: { span_count: 2, upper_bearing_members_per_span: 3 },
      groups: [expect.objectContaining({ quantity_key: "upper_bearing_members_per_span", quantity: 3 })],
    })));
  });

  it("lists extra generatable categories with a zero default that keeps the plan valid", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "beam");
    await userEvent.type(screen.getByLabelText("跨数"), "2");
    await userEvent.type(screen.getByLabelText("每跨上部承重构件数"), "3");
    await userEvent.selectOptions(screen.getByLabelText("对应构件类别"), "girder");

    expect(screen.getByText("其他部件（桥上没有的填 0）")).toBeInTheDocument();
    expect(screen.getByLabelText("桥面铺装数量")).toHaveValue(0);
    expect(screen.queryByLabelText("河床数量")).not.toBeInTheDocument();
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      input_quantities: { span_count: 2, upper_bearing_members_per_span: 3 },
      groups: [expect.objectContaining({ quantity_key: "upper_bearing_members_per_span" })],
    })));
  });

  it("adds a prefilled extra category group when its quantity is positive", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "beam");
    await userEvent.type(screen.getByLabelText("跨数"), "2");
    await userEvent.type(screen.getByLabelText("每跨上部承重构件数"), "3");
    await userEvent.selectOptions(screen.getByLabelText("对应构件类别"), "girder");

    const pavementCount = screen.getByLabelText("桥面铺装数量");
    await userEvent.clear(pavementCount);
    await userEvent.type(pavementCount, "1");
    expect(screen.getAllByDisplayValue("桥面铺装").length).toBe(2);

    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      input_quantities: { span_count: 2, upper_bearing_members_per_span: 3, pavement: 1 },
      groups: [
        expect.objectContaining({ quantity_key: "upper_bearing_members_per_span" }),
        expect.objectContaining({
          quantity_key: "pavement",
          standard_component_category_id: "pavement",
          structure_part: "deck_system",
          quantity: 1,
          site_name: "桥面铺装",
          site_component_type: "桥面铺装",
          numbering_mode: "sequential",
        }),
      ],
    })));
  });
});
