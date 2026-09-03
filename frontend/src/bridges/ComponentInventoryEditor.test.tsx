import { render, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import {
  confirmPendingComponentInventoryMappings,
  fetchInventoryGroupEntries,
  fetchInventorySummary,
  searchInventoryEntries,
  setComponentInventoryMapping,
  type InventoryGroupEntriesResponse,
  type InventoryGroupSummary as ServerGroupSummary,
  type InventorySummary,
  type LocatedInventoryEntry,
} from "../api/componentInventoryApi";
import {
  fetchStandardMappingCatalogs,
  type StandardCatalog,
} from "../api/standardsApi";
import { clearCachedForTests } from "../api/resourceCache";
import {
  ComponentInventoryEditor,
  groupAnomalyText,
  toGroupSummary,
} from "./ComponentInventoryEditor";

vi.mock("../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentInventoryApi")>();
  return {
    ...original,
    confirmPendingComponentInventoryMappings: vi.fn(),
    fetchInventorySummary: vi.fn(),
    fetchInventoryGroupEntries: vi.fn(),
    searchInventoryEntries: vi.fn(),
    setComponentInventoryMapping: vi.fn(),
  };
});

vi.mock("../api/standardsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardsApi")>();
  return { ...original, fetchStandardMappingCatalogs: vi.fn() };
});

const entry: LocatedInventoryEntry = {
  id: "entry-1", bridge_component_id: "internal-component-id", component_number: "1-1#",
  site_name: "主梁", site_component_type: "主梁", span_or_location: "第1跨", is_active: true,
  deactivated_at: null, deactivation_reason: null, sort_order: 1, remarks: null,
  is_referenced: true, position: 0,
  mappings: [{
    id: "mapping-1", standard_package_id: "package-1", standard_bridge_type_id: "beam",
    standard_component_category_id: "girder", structure_part: "superstructure",
    mapping_source: "自动生成", confirmation_status: "待确认", is_active: true,
  }],
};

const group: ServerGroupSummary = {
  site_component_type: "主梁", structure_part: "superstructure", active_count: 1,
  first_number: "1-1#", last_number: "1-1#", confirmed_count: 0, pending_count: 1,
  unmapped_count: 0, standard_package_id: "package-1",
  standard_component_category_id: "girder", standard_bridge_type_id: "beam",
};

const summary: InventorySummary = {
  revision: {
    id: "revision-1", bridge_id: "bridge-1", revision_number: 1, status: "草稿",
    baseline_revision_id: null, confirmed_at: null, active_entry_count: 1,
  },
  groups: [group],
  // 唯一的构件挂着待确认映射：计数记 1，但样本为空——待确认那批由界面上
  // "N 个构件的规范映射待确认"那一行代表，不进逐条列表。
  blockers: {
    total: 1, individual_total: 0,
    by_code: { inventory_empty: 0, component_mapping_required: 1 }, samples: [],
  },
};

// 全部映射确认之后的汇总，几处写操作用例共用。
const confirmedSummary: InventorySummary = {
  ...summary,
  groups: [{ ...group, confirmed_count: 1, pending_count: 0 }],
  blockers: { total: 0, individual_total: 0,
              by_code: { inventory_empty: 0, component_mapping_required: 0 }, samples: [] },
};

const groupPage: InventoryGroupEntriesResponse = {
  total: 1, page: 0, size: 20, entries: [entry],
};

const catalogs = [{
  package: { id: "package-1", standard_code: "JTG/T H21—2011" },
  component_categories: [{ id: "girder", name: "上部承重构件" }],
}] as unknown as StandardCatalog[];

describe("ComponentInventoryEditor", () => {
  beforeEach(() => {
    clearCachedForTests();  // 缓存是模块作用域的，不清会让用例顺序影响结果
    vi.resetAllMocks();
    vi.mocked(fetchInventorySummary).mockResolvedValue(summary);
    vi.mocked(fetchInventoryGroupEntries).mockResolvedValue(groupPage);
    vi.mocked(searchInventoryEntries).mockResolvedValue({ total: 0, entries: [] });
    // 轻量目录决定映射列显示的是友好名称还是原始 ID，必须给出真实形状。
    vi.mocked(fetchStandardMappingCatalogs).mockResolvedValue(catalogs);
    const confirmedEntry: LocatedInventoryEntry = {
      ...entry,
      mappings: [{ ...entry.mappings[0], confirmation_status: "已确认" }],
    };
    vi.mocked(setComponentInventoryMapping).mockResolvedValue({
      ...summary,
      groups: [{ ...group, confirmed_count: 1, pending_count: 0 }],
      blockers: { total: 0, individual_total: 0,
                  by_code: { inventory_empty: 0, component_mapping_required: 0 }, samples: [] },
      entry: confirmedEntry,
    });
  });

  // 切换页签会卸载路由组件；再挂载时应立即用上次结果渲染，同时后台重新校验，
  // 而不是每次都从"加载中…"开始等几秒。
  it("renders from cache on remount while still revalidating", async () => {
    const first = render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await screen.findByRole("heading", { name: "实际构件台账" });
    expect(fetchInventorySummary).toHaveBeenCalledTimes(1);
    first.unmount();

    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    // 立即可见，无需等待，也不出现加载态。
    expect(screen.getByRole("heading", { name: "实际构件台账" })).toBeInTheDocument();
    expect(screen.queryByText("正在加载台账与规范映射…")).not.toBeInTheDocument();
    // 但仍然重新拉了一次，避免停留在过期数据上。
    await waitFor(() => expect(fetchInventorySummary).toHaveBeenCalledTimes(2));
  });

  it("renders the archive-oriented ledger columns while keeping compact edit mode", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    // 左侧分类来自映射的 structure_part，顺序与向导一致。
    expect((await screen.findAllByText("上部结构")).length).toBeGreaterThan(0);

    const listPanel = screen.getByRole("heading", { name: "构件列表" }).closest("section") as HTMLElement;
    expect(within(listPanel).getByRole("columnheader", { name: "现场名称" })).toBeInTheDocument();
    expect(within(listPanel).getByRole("columnheader", { name: "规范映射" })).toBeInTheDocument();
    expect(await within(listPanel).findByRole("link", { name: "查看档案" }))
      .toHaveAttribute("href", "/bridges/bridge-1/components/internal-component-id");
    expect(within(listPanel).getByText("JTG/T H21—2011 · 上部承重构件")).toBeInTheDocument();

    // 行默认只读，字段要进编辑态才出现。
    await userEvent.click(await screen.findByRole("button", { name: "编辑" }));
    // 编辑区仍保持紧凑：现场名称由构件类别同步，不增加重复表单项。
    expect(await screen.findByLabelText("构件类别 1-1#")).toBeInTheDocument();
    expect(screen.queryByLabelText("现场名称 1-1#")).not.toBeInTheDocument();
  });

  // 规范目录是另一条请求。台账现在能瞬时渲染，目录还在路上的那段窗口里，
  // 不能把 h21.component.* 这类原始 ID 当作映射名显示给用户。
  it("leaves the mapping label empty until the standard catalog arrives", () => {
    expect(toGroupSummary(group, []).mappingLabel).toBe("");
    expect(toGroupSummary(group, catalogs).mappingLabel).toBe("JTG/T H21—2011 · 上部承重构件");
  });

  it("uses a newer catalog with the same stable category id for a historical mapping", () => {
    // 映射记的是历史规范包，但类别 id 稳定；换一个包版本仍要解析出同一个名称。
    const newerCatalog = [{
      package: { id: "package-2", standard_code: "JTG/T H21—2011" },
      component_categories: [{ id: "girder", name: "上部承重构件" }],
    }] as unknown as StandardCatalog[];

    expect(toGroupSummary(group, newerCatalog).mappingLabel)
      .toBe("JTG/T H21—2011 · 上部承重构件");
  });

  it("shows the inventory and mapping labels together on the first visit", async () => {
    let resolveCatalogs!: (catalogs: StandardCatalog[]) => void;
    vi.mocked(fetchStandardMappingCatalogs).mockReturnValue(new Promise((resolve) => {
      resolveCatalogs = resolve;
    }));

    render(<ComponentInventoryEditor bridgeId="bridge-1" />);

    expect(await screen.findByText("正在加载台账与规范映射…")).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "实际构件台账" })).toBeInTheDocument();
    resolveCatalogs([{
      package: { id: "package-1", standard_code: "JTG/T H21—2011" },
      component_categories: [{ id: "girder", name: "上部承重构件" }],
    } as never]);

    expect(await screen.findByText(/JTG\/T H21—2011 · 上部承重构件/)).toBeInTheDocument();
    expect(screen.queryByText("—")).not.toBeInTheDocument();
    expect(fetchStandardMappingCatalogs).toHaveBeenCalledTimes(1);
  });

  it("reports nothing once every mapping is confirmed", () => {
    const base = {
      siteComponentType: "主梁", structurePart: "superstructure", activeCount: 3,
      firstNumber: "1-1#", lastNumber: "1-3#", mappingLabel: "", unmappedCount: 0,
    };
    // 全确认时返回 null，表格就不为它渲染任何标记——数量列已经写过同一个数字。
    expect(groupAnomalyText({ ...base, confirmedCount: 3, pendingCount: 0 })).toBeNull();
    expect(groupAnomalyText({ ...base, confirmedCount: 1, pendingCount: 2 })).toBe("待确认 2");
    expect(groupAnomalyText({ ...base, confirmedCount: 0, pendingCount: 0, unmappedCount: 3 }))
      .toBe("无映射 3");
  });

  it("keeps internal component ids hidden and referenced entries deactivate-only", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await userEvent.click(await screen.findByRole("button", { name: "查看构件 主梁" }));
    await userEvent.click(await screen.findByRole("button", { name: "编辑" }));
    const numberInput = await screen.findByLabelText("构件编号 1-1#");
    const row = numberInput.closest("tr") as HTMLElement;
    expect(screen.queryByText("internal-component-id")).not.toBeInTheDocument();

    // 被引用的构件只能停用、不能删除；停用与删除都收在"更多"里。
    await userEvent.click(within(row).getByLabelText("更多操作 1-1#"));
    expect(within(row).queryByRole("button", { name: "删除" })).not.toBeInTheDocument();
    // 停用原因不再每行常驻，点了"停用"才问。
    expect(within(row).queryByLabelText("停用原因 1-1#")).not.toBeInTheDocument();
    await userEvent.click(within(row).getByRole("button", { name: "停用" }));

    expect(within(row).getByRole("button", { name: "确认停用" })).toBeDisabled();
    await userEvent.type(within(row).getByLabelText("停用原因 1-1#"), "构件已拆换");
    expect(within(row).getByRole("button", { name: "确认停用" })).toBeEnabled();
  });

  it("centralizes unresolved mappings and confirms an existing generated mapping", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    expect(await screen.findByText(/确认前还需处理 1 项/)).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "查看构件 主梁" }));
    // 映射相关动作收进了编辑态的"更多"。
    await userEvent.click(await screen.findByRole("button", { name: "编辑" }));
    await userEvent.click(await screen.findByLabelText("更多操作 1-1#"));
    await userEvent.click(await screen.findByRole("button", { name: "确认映射" }));
    expect(setComponentInventoryMapping).toHaveBeenCalledWith(expect.any(String), "revision-1", "entry-1", expect.objectContaining({
      standard_component_category_id: "girder",
      mapping_source: "用户确认",
    }));
    expect(await screen.findByText(/规范映射均已确认/)).toBeInTheDocument();
  });

  it("confirms pending mappings by group and in one click", async () => {
    vi.mocked(confirmPendingComponentInventoryMappings).mockResolvedValue(confirmedSummary);
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);

    expect(await screen.findByText(/1 个构件的规范映射待确认/)).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "实际构件台账" })).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "确认该组映射" }));
    expect(confirmPendingComponentInventoryMappings).toHaveBeenCalledWith(
      expect.any(String), "revision-1", "主梁");
    expect(await screen.findByText(/规范映射均已确认/)).toBeInTheDocument();

    vi.mocked(fetchInventorySummary).mockResolvedValue(summary);
    vi.mocked(confirmPendingComponentInventoryMappings).mockClear();
    vi.mocked(confirmPendingComponentInventoryMappings).mockResolvedValue(confirmedSummary);
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await userEvent.click(await screen.findByRole("button", { name: "一键确认全部待确认映射" }));
    expect(confirmPendingComponentInventoryMappings).toHaveBeenCalledWith(
      expect.any(String), "revision-1", undefined);
  });

  it("shows entry rows inline for the selected group or via number search", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await screen.findByRole("heading", { name: "实际构件台账" });
    expect(screen.queryByRole("dialog")).not.toBeInTheDocument();

    // 第一类默认选中，构件直接显示在右侧列表中。
    const listPanel = screen.getByRole("heading", { name: "构件列表" }).closest("section") as HTMLElement;
    expect(await within(listPanel).findByText("1-1#")).toBeInTheDocument();

    vi.mocked(searchInventoryEntries).mockResolvedValue({ total: 1, entries: [entry] });
    await userEvent.type(screen.getByLabelText("搜索构件"), "1-1");
    const results = await screen.findByRole("heading", { name: "搜索结果" });
    const section = results.closest("section") as HTMLElement;
    // 搜索防抖 250ms 后才发请求，结果是异步到达的。
    expect(await within(section).findByText("1-1#")).toBeInTheDocument();
    expect(await screen.findByText(/匹配 1 条/)).toBeInTheDocument();
  });

  it("fetches one page at a time instead of slicing a full list", async () => {
    // 分页现在在服务端做：翻页要真的再发一次请求，而不是在本地切数组。
    const pageOf = (index: number): InventoryGroupEntriesResponse => ({
      total: 120, page: index, size: 20,
      entries: Array.from({ length: 20 }, (_, offset) => ({
        ...entry,
        id: `entry-${index * 20 + offset + 1}`,
        component_number: `${index * 20 + offset + 1}#`,
        position: index * 20 + offset,
        mappings: [{ ...entry.mappings[0], confirmation_status: "已确认" }],
      })),
    });
    vi.mocked(fetchInventoryGroupEntries).mockImplementation(
      async (_base, _revisionId, _group, page) => pageOf(page));

    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    const listPanel = (await screen.findByRole("heading", { name: "构件列表" })).closest("section") as HTMLElement;
    expect(await within(listPanel).findByText("1#")).toBeInTheDocument();
    expect(within(listPanel).queryByText("21#")).not.toBeInTheDocument();
    expect(screen.getAllByText("共 120 条")).toHaveLength(2);

    await userEvent.click(screen.getByTitle("2"));
    expect(await within(listPanel).findByText("21#")).toBeInTheDocument();
    expect(within(listPanel).queryByText("1#")).not.toBeInTheDocument();
    // 第二页是另一次请求，不是本地切片。
    expect(fetchInventoryGroupEntries).toHaveBeenCalledWith(
      expect.any(String), "revision-1", "主梁", 1, 20, expect.anything());
  });

  it("maps server group fields onto the review table shape", () => {
    // 分组汇总由服务端算，这里只负责翻形状。整组停用时服务端给 null 编号范围，
    // 界面按空串走原有的破折号分支。
    const emptied: ServerGroupSummary = {
      ...group, site_component_type: "桥墩", active_count: 0,
      first_number: null, last_number: null,
      confirmed_count: 0, pending_count: 0, unmapped_count: 0,
      standard_package_id: null, standard_component_category_id: null,
    };

    expect(toGroupSummary(group, catalogs)).toEqual({
      siteComponentType: "主梁", structurePart: "superstructure", activeCount: 1,
      firstNumber: "1-1#", lastNumber: "1-1#",
      mappingLabel: "JTG/T H21—2011 · 上部承重构件",
      confirmedCount: 0, pendingCount: 1, unmappedCount: 0,
    });
    expect(toGroupSummary(emptied, catalogs)).toEqual({
      siteComponentType: "桥墩", structurePart: "superstructure", activeCount: 0,
      firstNumber: "", lastNumber: "", mappingLabel: "",
      confirmedCount: 0, pendingCount: 0, unmappedCount: 0,
    });
  });


  // 页面只发一次汇总请求，并仅预取默认分类的第一页；不会把整份五千多条台账拉下来。
  it("loads the summary and only the first page of the default category", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await screen.findByRole("heading", { name: "实际构件台账" });
    await screen.findByText("1-1#");

    expect(fetchInventorySummary).toHaveBeenCalledTimes(1);
    expect(fetchInventoryGroupEntries).toHaveBeenCalledTimes(1);
    expect(fetchInventoryGroupEntries).toHaveBeenCalledWith(
      expect.any(String), "revision-1", "主梁", 0, 20, expect.anything());
    expect(searchInventoryEntries).not.toHaveBeenCalled();
  });

  // 写操作的响应自带新汇总；拿到之后还要把当前打开的那一组重取一遍——只把响应里
  // 那条构件补进去是不够的，删除和批量确认根本不带构件，改类别还会让构件换组。
  it("replaces the summary from the write response and refetches the open group", async () => {
    render(<ComponentInventoryEditor bridgeId="bridge-1" />);
    await screen.findByText("1-1#");
    expect(fetchInventoryGroupEntries).toHaveBeenCalledTimes(1);

    await userEvent.click(await screen.findByRole("button", { name: "编辑" }));
    await userEvent.click(await screen.findByRole("button", { name: "确认映射" }));

    // 汇总换成写响应里的那份，界面立刻反映"全部已确认"。
    expect(await screen.findByText(/规范映射均已确认/)).toBeInTheDocument();
    // 当前分组被重取，而不是靠本地打补丁。
    await waitFor(() => expect(fetchInventoryGroupEntries).toHaveBeenCalledTimes(2));
    // 全程没有再拉一遍汇总——它是随写响应回来的。
    expect(fetchInventorySummary).toHaveBeenCalledTimes(1);
  });

});
