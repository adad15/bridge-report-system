import { render, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import {
  confirmPendingComponentInventoryMappings,
  fetchLatestComponentInventory,
  setComponentInventoryMapping,
  type ComponentInventoryRevision,
} from "../api/componentInventoryApi";
import { fetchStandardCatalog, fetchStandardPackages } from "../api/standardsApi";
import { clearCachedForTests } from "../api/resourceCache";
import {
  ComponentInventoryEditor,
  groupStatusText,
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
  return { ...original, fetchStandardPackages: vi.fn(), fetchStandardCatalog: vi.fn() };
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
    clearCachedForTests();  // 缓存是模块作用域的，不清会让用例顺序影响结果
    vi.resetAllMocks();
    vi.mocked(fetchLatestComponentInventory).mockResolvedValue(revision);
    // 规范目录决定映射列显示的是友好名称还是原始 ID，必须给出真实形状。
    vi.mocked(fetchStandardPackages).mockResolvedValue([
      { id: "package-1", family: "technical_condition", is_enabled: true, sync_status: "正常" } as never,
    ]);
    vi.mocked(fetchStandardCatalog).mockResolvedValue({
      package: { id: "package-1", standard_code: "JTG/T H21—2011" },
      component_categories: [{ id: "girder", name: "上部承重构件" }],
    } as never);
    vi.mocked(setComponentInventoryMapping).mockResolvedValue({
      ...revision,
      entries: [{ ...revision.entries[0], mappings: [{ ...revision.entries[0].mappings[0], confirmation_status: "已确认" }] }],
    });
  });

  // 切换页签会卸载路由组件；再挂载时应立即用上次结果渲染，同时后台重新校验，
  // 而不是每次都从"加载中…"开始等几秒。
  it("renders from cache on remount while still revalidating", async () => {
    const first = render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await screen.findByText("分组核对");
    expect(fetchLatestComponentInventory).toHaveBeenCalledTimes(1);
    first.unmount();

    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    // 立即可见，无需等待，也不出现加载态。
    expect(screen.getByText("分组核对")).toBeInTheDocument();
    expect(screen.queryByText("加载中…")).not.toBeInTheDocument();
    // 但仍然重新拉了一次，避免停留在过期数据上。
    await waitFor(() => expect(fetchLatestComponentInventory).toHaveBeenCalledTimes(2));
  });

  it("groups the review table by structure part and drops the redundant 现场名称 column", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    // 分部表头来自映射的 structure_part，顺序与向导一致。
    expect(await screen.findByRole("columnheader", { name: "上部结构" })).toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "查看构件 主梁" }));
    // 现场名称与构件类别生成时同值，页面只留构件类别。
    expect(await screen.findByLabelText("构件类别 1-1#")).toBeInTheDocument();
    expect(screen.queryByLabelText("现场名称 1-1#")).not.toBeInTheDocument();

    // 整组共用同一个映射：弹窗标题说明一次，不再逐行占一列。
    const dialog = screen.getByRole("dialog");
    expect(within(dialog).getByText(/规范映射：/)).toBeInTheDocument();
    expect(within(dialog).queryByRole("columnheader", { name: "规范映射" })).not.toBeInTheDocument();
  });

  // 规范目录是另一条请求。台账现在能瞬时渲染，目录还在路上的那段窗口里，
  // 不能把 h21.component.* 这类原始 ID 当作映射名显示给用户。
  it("leaves the mapping label empty until the standard catalog arrives", () => {
    const withoutCatalogs = inventoryGroupSummaries(revision, []);
    expect(withoutCatalogs[0].mappingLabel).toBe("");

    const withCatalogs = inventoryGroupSummaries(revision, [{
      package: { id: "package-1", standard_code: "JTG/T H21—2011" },
      component_categories: [{ id: "girder", name: "上部承重构件" }],
    } as never]);
    expect(withCatalogs[0].mappingLabel).toBe("JTG/T H21—2011 · 上部承重构件");
  });

  it("uses a newer catalog with the same stable category id for a historical mapping", () => {
    const summaries = inventoryGroupSummaries(revision, [{
      package: { id: "package-2", standard_code: "JTG/T H21—2011" },
      component_categories: [{ id: "girder", name: "上部承重构件" }],
    } as never]);

    expect(summaries[0].mappingLabel).toBe("JTG/T H21—2011 · 上部承重构件");
  });

  it("keeps the usable catalog when a historical package directory is unavailable", async () => {
    vi.mocked(fetchStandardPackages).mockResolvedValue([
      { id: "package-1", family: "technical_condition", is_enabled: true, sync_status: "正常" } as never,
      { id: "package-2", family: "technical_condition", is_enabled: true, sync_status: "正常" } as never,
    ]);
    vi.mocked(fetchStandardCatalog).mockImplementation(async (_baseUrl, packageId) => {
      if (packageId === "package-1") throw new Error("historical package unavailable");
      return {
        package: { id: "package-2", standard_code: "JTG/T H21—2011" },
        component_categories: [{ id: "girder", name: "上部承重构件" }],
      } as never;
    });

    render(<ComponentInventoryEditor bridgeId="bridge-1" />);

    expect(await screen.findByText("JTG/T H21—2011 · 上部承重构件")).toBeInTheDocument();
    expect(screen.queryByText(/规范映射名称暂时取不到/)).not.toBeInTheDocument();
  });

  it("shows a check instead of restating the count once every mapping is confirmed", () => {
    const base = {
      siteComponentType: "主梁", structurePart: "superstructure", activeCount: 3,
      firstNumber: "1-1#", lastNumber: "1-3#", mappingLabel: "", unmappedCount: 0,
    };
    // 数量列已经写了同一个数字，全确认时不再重复"已确认 3"。
    expect(groupStatusText({ ...base, confirmedCount: 3, pendingCount: 0 })).toBe("✓");
    expect(groupStatusText({ ...base, confirmedCount: 1, pendingCount: 2 })).toBe("待确认 2");
    expect(groupStatusText({ ...base, confirmedCount: 0, pendingCount: 0, unmappedCount: 3 }))
      .toBe("无映射 3");
  });

  it("keeps internal component ids hidden and referenced entries deactivate-only", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await userEvent.click(await screen.findByRole("button", { name: "查看构件 主梁" }));
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
    await userEvent.click(screen.getByRole("button", { name: "查看构件 主梁" }));
    await userEvent.click(await screen.findByRole("button", { name: "确认映射" }));
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

  it("shows entry rows in a dialog for the opened group or via number search", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await screen.findByText("分组核对");
    expect(screen.queryByLabelText("构件编号 1-1#")).not.toBeInTheDocument();
    expect(screen.getByText(/在分组核对表中点击/)).toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "查看构件 主梁" }));
    expect(await screen.findByRole("dialog", { name: /主梁 构件（共 1 个）/ })).toBeInTheDocument();
    expect(screen.getByLabelText("构件编号 1-1#")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "关闭" }));
    expect(screen.queryByRole("dialog")).not.toBeInTheDocument();
    expect(screen.queryByLabelText("构件编号 1-1#")).not.toBeInTheDocument();

    await userEvent.type(screen.getByLabelText("按编号搜索构件"), "1-1");
    expect(await screen.findByLabelText("构件编号 1-1#")).toBeInTheDocument();
    expect(screen.getByText(/匹配 1 个构件/)).toBeInTheDocument();
  });

  it("paginates a large expanded group", async () => {
    const confirmedMapping = { ...revision.entries[0].mappings[0], confirmation_status: "已确认" };
    vi.mocked(fetchLatestComponentInventory).mockResolvedValue({
      ...revision,
      entries: Array.from({ length: 120 }, (_, index) => ({
        ...revision.entries[0],
        id: `entry-${index + 1}`,
        bridge_component_id: `component-${index + 1}`,
        component_number: `${index + 1}#`,
        mappings: [{ ...confirmedMapping, id: `mapping-${index + 1}` }],
      })),
    });
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await userEvent.click(await screen.findByRole("button", { name: "查看构件 主梁" }));

    expect(await screen.findByLabelText("构件编号 1#")).toBeInTheDocument();
    expect(screen.queryByLabelText("构件编号 101#")).not.toBeInTheDocument();
    expect(screen.getByText("第 1 / 2 页")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "下一页" }));
    expect(await screen.findByLabelText("构件编号 101#")).toBeInTheDocument();
    expect(screen.queryByLabelText("构件编号 1#")).not.toBeInTheDocument();
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
        // 目录未加载（此处传空数组）时不给标签，避免显示原始类别 ID。
        mappingLabel: "",
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
