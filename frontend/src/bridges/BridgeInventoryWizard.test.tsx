import { useState } from "react";
import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchPartCatalog } from "../api/componentInventoryApi";
import { BridgeInventoryWizard, emptyInventorySelection } from "./BridgeInventoryWizard";

vi.mock("../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentInventoryApi")>();
  return { ...original, fetchPartCatalog: vi.fn() };
});

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
    part_key: "lower.riverbed", default_name: "河床", structure_part: "substructure" as const,
    standard_component_category_id: "h21.component.lower.riverbed",
    standard_component_category_name: "河床",
    number_template: "{name}", provisional: false, instance_selectable: false,
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

// 勾选状态住在弹窗里，测试里用这个壳子代替它持有。
function Harness({
  bridgeTypeId = "h21.bridge_type.beam",
  spanCount = 5,
  onPlanChange = () => {},
}: {
  bridgeTypeId?: string;
  spanCount?: number;
  onPlanChange?: Parameters<typeof BridgeInventoryWizard>[0]["onPlanChange"];
}) {
  const [selection, setSelection] = useState(emptyInventorySelection);
  return (
    <BridgeInventoryWizard
      packageId="package-1"
      bridgeTypeId={bridgeTypeId}
      spanCount={spanCount}
      selection={selection}
      onSelectionChange={setSelection}
      onPlanChange={onPlanChange}
    />
  );
}

describe("BridgeInventoryWizard", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchPartCatalog).mockImplementation(async (_base, _pkg, bridgeTypeId) =>
      bridgeTypeId === "h21.bridge_type.cable_stayed" ? cableParts : beamParts);
  });

  it("groups parts by 结构分部 only and keeps the category inline", async () => {
    render(<Harness />);
    expect(await screen.findByRole("heading", { name: "上部结构" })).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "下部结构" })).toBeInTheDocument();
    // 类别不再是标题，只是行内一段灰字。
    expect(screen.queryByRole("heading", { name: "上部承重构件" })).not.toBeInTheDocument();
    expect(screen.getByText("上部承重构件")).toBeInTheDocument();
    expect(screen.getByLabelText("启用 湿接缝")).toBeInTheDocument();
  });

  it("emits part_selections with counts and previews the first number", async () => {
    const onPlanChange = vi.fn();
    render(<Harness onPlanChange={onPlanChange} />);

    await userEvent.click(await screen.findByLabelText("启用 梁"));
    await userEvent.type(screen.getByLabelText("梁 每孔梁片数"), "13");

    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(
      expect.objectContaining({
        standard_package_id: "package-1",
        bridge_type_id: "h21.bridge_type.beam",
        span_count: 5,
        part_selections: [{ part_key: "beam.girder", site_name: "梁", counts: [13] }],
      }),
      expect.objectContaining({ total: 65, partCount: 1, missing: [] })
    ));
    expect(screen.getByText("共 65 个")).toBeInTheDocument();
    expect(screen.getByText(/1-1#梁/)).toBeInTheDocument();
  });

  it("marks provisional parts and flows a rename into the generated number", async () => {
    const onPlanChange = vi.fn();
    render(<Harness bridgeTypeId="h21.bridge_type.cable_stayed" spanCount={3} onPlanChange={onPlanChange} />);

    await userEvent.click(await screen.findByLabelText("启用 索塔"));
    expect(screen.getByText("临时编号（待校准）")).toBeInTheDocument();

    const nameInput = screen.getByLabelText("索塔 名称");
    await userEvent.clear(nameInput);
    await userEvent.type(nameInput, "桥塔");
    await userEvent.type(screen.getByLabelText("索塔 索塔数量"), "2");

    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(
      expect.objectContaining({
        bridge_type_id: "h21.bridge_type.cable_stayed",
        part_selections: [{ part_key: "cs.tower", site_name: "桥塔", counts: [2] }],
      }),
      expect.objectContaining({ total: 2 })
    ));
    expect(screen.getByText(/1#桥塔/)).toBeInTheDocument();
  });

  it("counts big parts without expanding every number", async () => {
    const onPlanChange = vi.fn();
    render(<Harness spanCount={33} onPlanChange={onPlanChange} />);

    await userEvent.click(await screen.findByLabelText("启用 支座"));
    await userEvent.type(screen.getByLabelText("支座 每孔每墩支座数"), "50");

    // 33 孔 × 2 支承 × 每墩 50 个。旧版要展开 3300 条才知道这个数。
    expect(await screen.findByText("共 3300 个")).toBeInTheDocument();
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(
      expect.anything(),
      expect.objectContaining({ total: 3300 })
    ));
  });

  it("shows the disambiguating hint on the bearing count", async () => {
    render(<Harness spanCount={2} />);
    await userEvent.click(await screen.findByLabelText("启用 支座"));
    // 一个墩上落着相邻两孔的支座，标签与提示都必须说清只数一个孔的。
    expect(screen.getByLabelText("支座 每孔每墩支座数")).toBeInTheDocument();
    expect(screen.getByTitle(/只数一个孔落在这个墩上的支座/)).toBeInTheDocument();
  });

  it("lets the user drop wing wall positions the bridge does not have", async () => {
    const onPlanChange = vi.fn();
    render(<Harness spanCount={2} onPlanChange={onPlanChange} />);
    await userEvent.click(await screen.findByLabelText("启用 翼墙"));

    // 几何上 2 台 × 2 侧 = 4 个，逐个可勾选。
    for (const number of ["0#台左侧翼墙", "0#台右侧翼墙", "2#台左侧翼墙", "2#台右侧翼墙"])
      expect(screen.getByLabelText(number)).toBeChecked();

    await userEvent.click(screen.getByLabelText("0#台右侧翼墙"));
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(
      expect.objectContaining({
        part_selections: [
          { part_key: "lower.wing_wall", site_name: "翼墙", counts: [],
            excluded_numbers: ["0#台右侧翼墙"] },
        ],
      }),
      expect.objectContaining({ total: 3 })
    ));
    expect(screen.getByText("共 3 个")).toBeInTheDocument();
  });

  it("adds riverbed as one whole-bridge substructure entry", async () => {
    const onPlanChange = vi.fn();
    render(<Harness spanCount={33} onPlanChange={onPlanChange} />);

    await userEvent.click(await screen.findByLabelText("启用 河床"));
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(
      expect.objectContaining({
        part_selections: [{ part_key: "lower.riverbed", site_name: "河床", counts: [] }],
      }),
      expect.objectContaining({ total: 1, partCount: 1, missing: [] })
    ));
    expect(screen.getByText("共 1 个")).toBeInTheDocument();
    expect(screen.getByText(/河床…/)).toBeInTheDocument();
  });

  it("names the parts still missing a count instead of silently invalidating the plan", async () => {
    const onPlanChange = vi.fn();
    render(<Harness onPlanChange={onPlanChange} />);
    await userEvent.click(await screen.findByLabelText("启用 梁"));

    // 数量维未填 → 计划无效，但底部要说得出是哪个部件卡着。
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(
      null,
      expect.objectContaining({ partCount: 1, missing: ["梁"] })
    ));
    expect(screen.getByText("请填数量")).toBeInTheDocument();
  });

  it("filters the list by part or category name", async () => {
    render(<Harness />);
    expect(await screen.findByLabelText("启用 梁")).toBeInTheDocument();

    await userEvent.type(screen.getByLabelText("筛选部件"), "支座");
    expect(screen.getByLabelText("启用 支座")).toBeInTheDocument();
    expect(screen.queryByLabelText("启用 梁")).not.toBeInTheDocument();
  });

  it("reloads the part catalog when the bridge type changes", async () => {
    const { rerender } = render(<Harness />);
    expect(await screen.findByLabelText("启用 梁")).toBeInTheDocument();

    rerender(<Harness bridgeTypeId="h21.bridge_type.cable_stayed" />);
    expect(await screen.findByLabelText("启用 索塔")).toBeInTheDocument();
    expect(screen.queryByLabelText("启用 梁")).not.toBeInTheDocument();
  });
});
