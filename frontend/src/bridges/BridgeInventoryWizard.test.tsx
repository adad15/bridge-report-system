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
    standard_component_category_name: "上部承重构件",
    number_template: "{span}-{c1}#{name}", provisional: false, instance_selectable: false,
    count_inputs: [{ key: "girders_per_span", label: "每孔梁片数", hint: "" }],
  },
  {
    part_key: "beam.wet_joint", default_name: "湿接缝", structure_part: "superstructure" as const,
    standard_component_category_id: "h21.component.beam.upper_general",
    standard_component_category_name: "上部一般构件",
    number_template: "{span}-{c1}#{name}", provisional: false, instance_selectable: false,
    count_inputs: [{ key: "joints_per_span", label: "每孔湿接缝条数", hint: "" }],
  },
  {
    part_key: "beam.diaphragm", default_name: "横隔梁", structure_part: "superstructure" as const,
    standard_component_category_id: "h21.component.beam.upper_general",
    standard_component_category_name: "上部一般构件",
    number_template: "{span}-{c1}-{c2}#{name}", provisional: false, instance_selectable: false,
    count_inputs: [
      { key: "gaps", label: "每孔梁间数", hint: "" },
      { key: "beams", label: "每梁间道数", hint: "" },
    ],
  },
  {
    part_key: "bearing.support", default_name: "支座", structure_part: "superstructure" as const,
    standard_component_category_id: "h21.component.bearing",
    standard_component_category_name: "支座",
    number_template: "{span}-{sup}-{c1}#{name}", provisional: false, instance_selectable: false,
    count_inputs: [
      { key: "bearings_per_pier", label: "每孔每墩支座数", hint: "只数一个孔落在这个墩上的支座。" },
    ],
  },
  {
    part_key: "lower.wing_wall", default_name: "翼墙", structure_part: "substructure" as const,
    standard_component_category_id: "h21.component.lower.wing_or_ear_wall",
    standard_component_category_name: "翼墙、耳墙",
    number_template: "{ab}#台{side}侧{name}", provisional: false, instance_selectable: true,
    count_inputs: [],
  },
  {
    part_key: "deck.drainage", default_name: "排水系统", structure_part: "deck_system" as const,
    standard_component_category_id: "h21.component.deck.drainage",
    standard_component_category_name: "排水系统",
    number_template: "{name}", provisional: false, instance_selectable: false, count_inputs: [],
  },
];

const cableParts = [
  {
    part_key: "cs.tower", default_name: "索塔", structure_part: "superstructure" as const,
    standard_component_category_id: "h21.component.cable_stayed.tower",
    standard_component_category_name: "索塔",
    number_template: "{c1}#{name}", provisional: true, instance_selectable: false,
    count_inputs: [{ key: "tower_count", label: "索塔数量", hint: "" }],
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

  it("nests parts under 结构分部 then 部件类别 headings", async () => {
    render(<BridgeInventoryWizard onPlanChange={vi.fn()} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "h21.bridge_type.beam");
    expect(await screen.findByRole("heading", { name: "上部结构" })).toBeInTheDocument();
    // 部件类别表头：承重在前、一般在后；一般类别下含湿接缝 + 横隔梁两个部件。
    expect(screen.getByRole("heading", { name: "上部承重构件" })).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "上部一般构件" })).toBeInTheDocument();
    expect(screen.getByLabelText("启用 湿接缝")).toBeInTheDocument();
    expect(screen.getByLabelText("启用 横隔梁")).toBeInTheDocument();
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

  it("shows the disambiguating hint on the bearing count", async () => {
    render(<BridgeInventoryWizard onPlanChange={vi.fn()} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "h21.bridge_type.beam");
    await userEvent.type(screen.getByLabelText("跨数"), "2");
    await userEvent.click(await screen.findByLabelText("启用 支座"));
    // 一个墩上落着相邻两孔的支座，标签与提示都必须说清只数一个孔的。
    expect(screen.getByLabelText("支座 每孔每墩支座数")).toBeInTheDocument();
    expect(screen.getByText(/只数一个孔落在这个墩上的支座/)).toBeInTheDocument();
  });

  it("lets the user drop wing wall positions the bridge does not have", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "h21.bridge_type.beam");
    await userEvent.type(screen.getByLabelText("跨数"), "2");
    await userEvent.click(await screen.findByLabelText("启用 翼墙"));

    // 几何上 2 台 × 2 侧 = 4 个，逐个可勾选。
    for (const number of ["0#台左侧翼墙", "0#台右侧翼墙", "2#台左侧翼墙", "2#台右侧翼墙"])
      expect(screen.getByLabelText(number)).toBeChecked();

    await userEvent.click(screen.getByLabelText("0#台右侧翼墙"));
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      part_selections: [
        { part_key: "lower.wing_wall", site_name: "翼墙", counts: [],
          excluded_numbers: ["0#台右侧翼墙"] },
      ],
    })));
    expect(screen.getByText("共 3 个")).toBeInTheDocument();
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
