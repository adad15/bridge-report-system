import { useEffect } from "react";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { createBridge } from "../api/bridgeAdministrationApi";
import { generateComponentInventory } from "../api/componentInventoryApi";
import { fetchStandardCatalog, fetchStandardPackages } from "../api/standardsApi";
import { CreateBridgeDialog } from "./CreateBridgeDialog";
import type { InventorySelection, InventorySummary } from "./BridgeInventoryWizard";

vi.mock("../api/bridgeAdministrationApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/bridgeAdministrationApi")>();
  return { ...original, createBridge: vi.fn() };
});

vi.mock("../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentInventoryApi")>();
  return { ...original, generateComponentInventory: vi.fn() };
});

vi.mock("../api/standardsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardsApi")>();
  return { ...original, fetchStandardCatalog: vi.fn(), fetchStandardPackages: vi.fn() };
});

const packageSummary = {
  id: "package-1", family: "technical_condition" as const, standard_id: "H21",
  standard_code: "JTG/T H21—2011", standard_name: "公路桥梁技术状况评定标准",
  official_edition: "2011", package_version: "1.0.4", contract_version: 1,
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

const plan = {
  standard_package_id: "package-1", bridge_type_id: "h21.bridge_type.beam", span_count: 5,
  part_selections: [{ part_key: "beam.girder", site_name: "主梁", counts: [1] }],
};

const summary: InventorySummary = { total: 165, partCount: 1, missing: [] };

// 只替掉组件本身，emptyInventorySelection / validSpanCount 这些还得用真的。
vi.mock("./BridgeInventoryWizard", async (importOriginal) => {
  const original = await importOriginal<typeof import("./BridgeInventoryWizard")>();
  return {
    ...original,
    BridgeInventoryWizard: ({
      selection,
      onSelectionChange,
      onPlanChange,
    }: {
      selection: InventorySelection;
      onSelectionChange: (next: InventorySelection) => void;
      onPlanChange: (value: typeof plan, next: InventorySummary) => void;
    }) => {
      useEffect(() => onPlanChange(plan, summary), [onPlanChange]);
      return (
        <div>
          <p>已勾 {Object.values(selection.enabled).filter(Boolean).length} 个</p>
          <button
            type="button"
            onClick={() =>
              onSelectionChange({ ...selection, enabled: { ...selection.enabled, "beam.girder": true } })
            }
          >
            勾一个部件
          </button>
        </div>
      );
    },
  };
});

async function fillBase() {
  await userEvent.type(screen.getByLabelText("桥梁名称"), "测试桥");
  await userEvent.selectOptions(await screen.findByLabelText("桥型"), "h21.bridge_type.beam");
  await userEvent.type(screen.getByLabelText("跨数"), "5");
}

describe("CreateBridgeDialog", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchStandardPackages).mockResolvedValue([packageSummary]);
    vi.mocked(fetchStandardCatalog).mockResolvedValue(catalog);
    vi.mocked(createBridge).mockResolvedValue({
      id: "bridge-1", system_number: "QL-000001", bridge_name: "测试桥", route_number: null,
      route_name: null, administrative_region: null, station_mark: null, status: "在用", bridge_scale: null,
    });
    vi.mocked(generateComponentInventory).mockResolvedValue({
      revision: {
        id: "revision-1", bridge_id: "bridge-1", revision_number: 1, status: "draft",
        baseline_revision_id: null, confirmed_at: null, active_entry_count: 0,
      },
      groups: [],
      blockers: {
        total: 0, individual_total: 0,
        by_code: { inventory_empty: 0, component_mapping_required: 0 }, samples: [],
      },
    });
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("creates the bridge and its draft inventory in order", async () => {
    const onCreated = vi.fn();
    render(<CreateBridgeDialog onClose={vi.fn()} onCreated={onCreated} />);
    await fillBase();
    await userEvent.click(screen.getByRole("button", { name: "下一步：构件台账" }));
    await userEvent.click(await screen.findByRole("button", { name: "创建桥梁并生成台账" }));

    expect(createBridge).toHaveBeenCalledWith(expect.any(String), expect.objectContaining({ bridge_name: "测试桥" }));
    expect(generateComponentInventory).toHaveBeenCalledWith(expect.any(String), "bridge-1", plan);
    expect(onCreated).toHaveBeenCalledWith(expect.objectContaining({ id: "bridge-1" }));
  });

  it("holds 桥型 and 跨数 on the first step, next to the other bridge facts", async () => {
    render(<CreateBridgeDialog onClose={vi.fn()} onCreated={vi.fn()} />);
    expect(await screen.findByLabelText("桥型")).toBeInTheDocument();
    expect(screen.getByLabelText("跨数")).toBeInTheDocument();
    // 只有一个规范包时自动选中，不再单独占一个下拉框。
    expect(screen.queryByLabelText("初始台账规范来源")).not.toBeInTheDocument();
    expect(screen.getByText(/不会绑定或限制以后检测项目采用的评分规范/)).toBeInTheDocument();
  });

  it("defaults to the newest enabled package and shows versions when history is available", async () => {
    const oldPackage = { ...packageSummary, id: "package-old", package_version: "1.0.3" };
    vi.mocked(fetchStandardPackages).mockResolvedValue([oldPackage, packageSummary]);
    vi.mocked(fetchStandardCatalog).mockImplementation(async (_base, id) => ({
      ...catalog,
      package: id === oldPackage.id ? oldPackage : packageSummary,
    }));

    render(<CreateBridgeDialog onClose={vi.fn()} onCreated={vi.fn()} />);
    const select = await screen.findByLabelText("初始台账规范来源");
    expect(select).toHaveValue("package-1");
    expect(screen.getByRole("option", { name: /v1\.0\.4/ })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: /v1\.0\.3/ })).toBeInTheDocument();
  });

  it("keeps 下一步 disabled until the name, the bridge type and the span count are all there", async () => {
    render(<CreateBridgeDialog onClose={vi.fn()} onCreated={vi.fn()} />);
    const next = await screen.findByRole("button", { name: "下一步：构件台账" });
    expect(next).toBeDisabled();

    await userEvent.type(screen.getByLabelText("桥梁名称"), "测试桥");
    expect(next).toBeDisabled();
    await userEvent.selectOptions(screen.getByLabelText("桥型"), "h21.bridge_type.beam");
    expect(next).toBeDisabled();
    await userEvent.type(screen.getByLabelText("跨数"), "5");
    expect(next).toBeEnabled();
  });

  it("reports what will be generated in the footer", async () => {
    render(<CreateBridgeDialog onClose={vi.fn()} onCreated={vi.fn()} />);
    await fillBase();
    await userEvent.click(screen.getByRole("button", { name: "下一步：构件台账" }));
    expect(await screen.findByText("将生成 165 个构件 · 1 个部件")).toBeInTheDocument();
  });

  it("keeps the part selection when the user steps back to fix a base field", async () => {
    render(<CreateBridgeDialog onClose={vi.fn()} onCreated={vi.fn()} />);
    await fillBase();
    await userEvent.click(screen.getByRole("button", { name: "下一步：构件台账" }));
    await userEvent.click(await screen.findByRole("button", { name: "勾一个部件" }));
    expect(screen.getByText("已勾 1 个")).toBeInTheDocument();

    // 回第一步改个桩号再回来，勾好的部件不能没。
    await userEvent.click(screen.getByRole("button", { name: "上一步" }));
    await userEvent.type(screen.getByLabelText("桩号"), "K109+747");
    await userEvent.click(screen.getByRole("button", { name: "下一步：构件台账" }));
    expect(await screen.findByText("已勾 1 个")).toBeInTheDocument();
  });

  it("asks before dropping the part selection on a bridge type change", async () => {
    const confirm = vi.fn().mockReturnValue(false);
    vi.stubGlobal("confirm", confirm);
    render(<CreateBridgeDialog onClose={vi.fn()} onCreated={vi.fn()} />);
    await fillBase();
    await userEvent.click(screen.getByRole("button", { name: "下一步：构件台账" }));
    await userEvent.click(await screen.findByRole("button", { name: "勾一个部件" }));
    await userEvent.click(screen.getByRole("button", { name: "上一步" }));

    await userEvent.selectOptions(screen.getByLabelText("桥型"), "h21.bridge_type.cable_stayed");
    expect(confirm).toHaveBeenCalledWith(expect.stringContaining("换桥型会清空已经勾选的 1 个部件"));
    // 拒绝之后桥型和勾选都得原样留着。
    expect(screen.getByLabelText("桥型")).toHaveValue("h21.bridge_type.beam");
    await userEvent.click(screen.getByRole("button", { name: "下一步：构件台账" }));
    expect(await screen.findByText("已勾 1 个")).toBeInTheDocument();
  });
});
