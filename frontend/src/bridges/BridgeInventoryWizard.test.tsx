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

async function fillBeamTemplate() {
  await userEvent.selectOptions(await screen.findByLabelText("桥型"), "beam");
  await userEvent.type(screen.getByLabelText("跨数"), "2");
  await userEvent.selectOptions(
    screen.getByLabelText("上部承重构件数（全桥） 对应构件类别"), "girder");
  await userEvent.type(screen.getByLabelText("上部承重构件数（全桥） 数量 1"), "6");
}

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
    expect(screen.queryByText("上部承重构件数（全桥）")).not.toBeInTheDocument();
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

  it("converts whole-bridge quantities into per-span groups for span numbering", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await fillBeamTemplate();

    expect(screen.getByLabelText("上部承重构件数（全桥） 构件名称 1")).toHaveValue("主梁");
    expect(screen.getByText("全桥合计 6")).toBeInTheDocument();
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      template_id: "beam-template",
      span_count: 2,
      input_quantities: { span_count: 2, upper_bearing_members_per_span: 3 },
      groups: [expect.objectContaining({
        quantity_key: "upper_bearing_members_per_span",
        quantity: 3,
        site_name: "主梁",
        site_component_type: "主梁",
        numbering_mode: "span_member",
      })],
    })));
  });

  it("divides pier-line numbering by span count minus one", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "beam");
    await userEvent.type(screen.getByLabelText("跨数"), "3");
    await userEvent.selectOptions(
      screen.getByLabelText("上部承重构件数（全桥） 对应构件类别"), "girder");
    await userEvent.type(screen.getByLabelText("上部承重构件数（全桥） 数量 1"), "6");
    await userEvent.selectOptions(
      screen.getByLabelText("上部承重构件数（全桥） 编号方式 1"), "pier_line");

    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      input_quantities: { span_count: 3, upper_bearing_members_per_span: 3 },
      groups: [expect.objectContaining({ quantity: 3, numbering_mode: "pier_line" })],
    })));

    const quantity = screen.getByLabelText("上部承重构件数（全桥） 数量 1");
    await userEvent.clear(quantity);
    await userEvent.type(quantity, "5");
    expect(
      screen.getByText("按墩位编号时，全桥数量必须能被“跨数减 1”整除（两端为桥台）。")
    ).toBeInTheDocument();
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(null));
  });

  it("previews pier-line numbers with span count minus one lines", () => {
    expect(previewInventoryNumbers([{
      site_component_type: "桥墩", site_name: "桥墩", standard_component_category_id: "pier",
      structure_part: "substructure", numbering_mode: "pier_line", quantity: 2,
      quantity_key: "pier_count", number_prefix: "", number_suffix: "#",
    }], 3)).toEqual([{
      quantityKey: "pier_count", siteType: "桥墩", count: 4,
      numbers: ["1-1#", "1-2#", "2-1#", "2-2#"],
    }]);
  });

  it("rejects a whole-bridge quantity that spans cannot divide evenly", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await userEvent.selectOptions(await screen.findByLabelText("桥型"), "beam");
    await userEvent.type(screen.getByLabelText("跨数"), "2");
    await userEvent.selectOptions(
      screen.getByLabelText("上部承重构件数（全桥） 对应构件类别"), "girder");
    await userEvent.type(screen.getByLabelText("上部承重构件数（全桥） 数量 1"), "3");

    expect(screen.getByText("按跨编号时，全桥数量必须能被跨数整除。")).toBeInTheDocument();
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(null));

    await userEvent.type(screen.getByLabelText("上部承重构件数（全桥） 数量 1"), "0");
    expect(screen.queryByText("按跨编号时，全桥数量必须能被跨数整除。")).not.toBeInTheDocument();
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      input_quantities: { span_count: 2, upper_bearing_members_per_span: 15 },
      groups: [expect.objectContaining({ quantity: 15 })],
    })));
  });

  it("splits one category into multiple kinds whose quantities sum into the template input", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await fillBeamTemplate();

    await userEvent.click(screen.getByLabelText("上部承重构件数（全桥） 添加一种构件"));
    await userEvent.type(screen.getByLabelText("上部承重构件数（全桥） 构件名称 2"), "横隔板");
    await userEvent.type(screen.getByLabelText("上部承重构件数（全桥） 数量 2"), "2");

    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      input_quantities: { span_count: 2, upper_bearing_members_per_span: 4 },
      groups: [
        expect.objectContaining({
          quantity_key: "upper_bearing_members_per_span", quantity: 3, site_component_type: "主梁",
        }),
        expect.objectContaining({
          quantity_key: "upper_bearing_members_per_span", quantity: 1, site_component_type: "横隔板",
        }),
      ],
    })));

    await userEvent.click(screen.getByLabelText("上部承重构件数（全桥） 移除 2"));
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      input_quantities: { span_count: 2, upper_bearing_members_per_span: 3 },
    })));
  });

  it("lists extra generatable categories with a zero default that keeps the plan valid", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await fillBeamTemplate();

    expect(screen.getByText("其他部件（桥上没有的填 0）")).toBeInTheDocument();
    expect(screen.getByLabelText("桥面铺装 数量 1")).toHaveValue(0);
    expect(screen.queryByText("河床")).not.toBeInTheDocument();
    await waitFor(() => expect(onPlanChange).toHaveBeenLastCalledWith(expect.objectContaining({
      input_quantities: { span_count: 2, upper_bearing_members_per_span: 3 },
      groups: [expect.objectContaining({ quantity_key: "upper_bearing_members_per_span" })],
    })));
  });

  it("adds a prefilled extra category group when its quantity is positive", async () => {
    const onPlanChange = vi.fn();
    render(<BridgeInventoryWizard onPlanChange={onPlanChange} />);
    await fillBeamTemplate();

    const pavementCount = screen.getByLabelText("桥面铺装 数量 1");
    await userEvent.clear(pavementCount);
    await userEvent.type(pavementCount, "1");
    expect(screen.getByLabelText("桥面铺装 构件名称 1")).toHaveValue("桥面铺装");

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

  it("keeps prefix and suffix inputs collapsed behind the affix details", async () => {
    render(<BridgeInventoryWizard onPlanChange={vi.fn()} />);
    await fillBeamTemplate();

    const affix = screen.getAllByText("编号前后缀")[0];
    expect(affix.closest("details")?.open).toBeFalsy();
    expect(screen.getByLabelText("上部承重构件数（全桥） 编号后缀 1")).toHaveValue("#");
  });
});
