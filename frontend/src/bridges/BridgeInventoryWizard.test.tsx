import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchPartCatalog } from "../api/componentInventoryApi";
import { fetchStandardCatalog, fetchStandardPackages } from "../api/standardsApi";
import { BridgeInventoryWizard } from "./BridgeInventoryWizard";

vi.mock("../api/standardsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardsApi")>();
  return { ...original, fetchStandardCatalog: vi.fn(), fetchStandardPackages: vi.fn() };
});

vi.mock("../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentInventoryApi")>();
  return { ...original, fetchPartCatalog: vi.fn() };
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
    { id: "h21.bridge_type.beam", code: "beam", name: "梁式桥" },
    { id: "h21.bridge_type.cable_stayed", code: "cable_stayed", name: "斜拉桥" },
  ],
  component_categories: [],
  inventory_templates: [],
  defect_catalogs: [], maintenance_levels: [], inspection_types: [], periodic_inspection_requirements: [],
};

const beamParts = [
  {
    part_key: "beam.girder", default_name: "梁", structure_part: "superstructure" as const,
    standard_component_category_id: "h21.component.beam.upper_bearing",
    number_template: "{span}-{c1}#{name}", provisional: false,
    count_inputs: [{ key: "girders_per_span", label: "每孔梁片数" }],
  },
  {
    part_key: "deck.drainage", default_name: "排水系统", structure_part: "deck_system" as const,
    standard_component_category_id: "h21.component.deck.drainage",
    number_template: "{name}", provisional: false, count_inputs: [],
  },
];

const cableParts = [
  {
    part_key: "cs.tower", default_name: "索塔", structure_part: "superstructure" as const,
    standard_component_category_id: "h21.component.cable_stayed.tower",
    number_template: "{c1}#{name}", provisional: true,
    count_inputs: [{ key: "tower_count", label: "索塔数量" }],
  },
];

describe("BridgeInventoryWizard", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchStandardPackages).mockResolvedValue([packageSummary]);
    vi.mocked(fetchStandardCatalog).mockResolvedValue(catalog);
    vi.mocked(fetchPartCatalog).mockImplementation(async (_base, _pkg, bridgeTypeId) =>
      bridgeTypeId === "h21.bridge_type.cable_stayed" ? cableParts : beamParts);
  });

  it("explains that the template source does not bind future scoring standards", async () => {
    render(<BridgeInventoryWizard onPlanChange={vi.fn()} />);
    expect(await screen.findByText(/不会绑定或限制以后检测项目采用的评分规范/)).toBeInTheDocument();
  });

  it("emits part_selections for enabled parts with counts and previews numbers", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "h21.bridge_type.beam");
    await userEvent.type(screen.getByLabelText("跨数"), "5");

    await userEvent.click(await screen.findByLabelText("启用 梁"));
    await userEvent.type(screen.getByLabelText("梁 每孔梁片数"), "13");

    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      standard_package_id: "package-1",
      bridge_type_id: "h21.bridge_type.beam",
      span_count: 5,
      part_selections: [{ part_key: "beam.girder", site_name: "梁", counts: [13] }],
    })));
    expect(screen.getByText(/1-1#梁/)).toBeInTheDocument();
  });

  it("marks provisional parts and flows a rename into the generated number", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "h21.bridge_type.cable_stayed");
    await userEvent.type(screen.getByLabelText("跨数"), "3");

    await userEvent.click(await screen.findByLabelText("启用 索塔"));
    expect(screen.getByText("临时编号（待校准）")).toBeInTheDocument();

    const nameInput = screen.getByLabelText("索塔 名称");
    await userEvent.clear(nameInput);
    await userEvent.type(nameInput, "桥塔");
    await userEvent.type(screen.getByLabelText("索塔 索塔数量"), "2");

    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      bridge_type_id: "h21.bridge_type.cable_stayed",
      part_selections: [{ part_key: "cs.tower", site_name: "桥塔", counts: [2] }],
    })));
    expect(screen.getByText(/1#桥塔/)).toBeInTheDocument();
  });

  it("clears selections and reloads parts when the bridge type changes", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    const bridgeType = await screen.findByLabelText("桥型");
    await userEvent.selectOptions(bridgeType, "h21.bridge_type.beam");
    await userEvent.type(screen.getByLabelText("跨数"), "5");
    await userEvent.click(await screen.findByLabelText("启用 梁"));

    await userEvent.selectOptions(bridgeType, "h21.bridge_type.cable_stayed");
    expect(screen.queryByLabelText("启用 梁")).not.toBeInTheDocument();
    expect(await screen.findByLabelText("启用 索塔")).toBeInTheDocument();
    expect(screen.getByLabelText("跨数")).toHaveValue(null);
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(null));
  });

  it("emits null until at least one enabled part has complete counts", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "h21.bridge_type.beam");
    await userEvent.type(screen.getByLabelText("跨数"), "5");
    await userEvent.click(await screen.findByLabelText("启用 梁"));

    // 数量维未填 → 计划无效。
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(null));
  });
});
