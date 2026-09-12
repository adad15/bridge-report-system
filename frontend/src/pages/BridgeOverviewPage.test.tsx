import { render, screen } from "@testing-library/react";
import { MemoryRouter } from "react-router-dom";
import { describe, expect, it, vi } from "vitest";

import { BridgeOverviewPage } from "./BridgeOverviewPage";

vi.mock("../workspace/BridgeWorkspaceShell", () => ({
  useBridgeWorkspace: () => ({
    overview: {
      bridge: { id: "bridge-1", bridge_name: "测试桥" },
      latest_inspection: null,
      pending: { total_count: 0, import_count: 0, unbound_observation_count: 0 },
      recent_inspections: [],
      structure_ratings: [],
      defect_archive: { component_count: 0, thread_count: 0, unbound_observation_count: 0 },
      defect_comparison: {
        available: true,
        previous_year: 2025,
        latest_year: 2026,
        previous_observation_count: 4,
        latest_observation_count: 7,
        increased_observation_count: 3,
        decreased_observation_count: 0,
        changed_component_count: 1,
        unchanged_component_count: 2,
        groups: [{
          structure_part: "桥面系",
          component_type: "桥面铺装",
          previous_count: 4,
          latest_count: 7,
          component_count: 3,
          changed_component_count: 1,
          defect_types: [
            { defect_type: "横向裂缝", previous_count: 3, latest_count: 4 },
            { defect_type: "网状裂缝", previous_count: 1, latest_count: 1 },
            // 源数据里确实有前后空格，展示前要收拾干净。
            { defect_type: "  坑槽 ", previous_count: 0, latest_count: 2 },
          ],
        }],
      },
    },
    reloadOverview: vi.fn(),
  }),
}));

vi.mock("../bridges/ComponentInventoryEditor", () => ({
  ComponentInventoryEditor: ({ bridgeId }: { bridgeId: string }) => <div>台账桥梁：{bridgeId}</div>,
}));

vi.mock("../auth/AuthContext", () => ({
  useAuth: () => ({ user: { username: "admin", display_name: "管理员", role: "admin" } }),
}));

// 桥梁概况卡片自己取数，它有自己的用例。
vi.mock("../bridges/BridgeProfileCard", () => ({
  BridgeProfileCard: ({ bridgeId }: { bridgeId: string }) => <div>桥梁概况：{bridgeId}</div>,
}));

describe("BridgeOverviewPage", () => {
  // 台账构件可达数千条，挂在总览页会让每次进桥、每次切回都先等它整份加载完。
  // 台账在 /bridges/:id/inventory，入口由工作区标签导航提供，总览页不碰它。
  it("does not load the inventory", () => {
    render(<MemoryRouter><BridgeOverviewPage /></MemoryRouter>);
    expect(screen.queryByText("台账桥梁：bridge-1")).not.toBeInTheDocument();
  });
  // 步骤条读的是"有没有正式结论"，结论一出就永远停在已完成；这块位置换成年度间的实际变化。
  it("describes each component type in prose: previous year first, then the latest", async () => {
    render(<MemoryRouter><BridgeOverviewPage /></MemoryRouter>);

    // findByText 命中的是 <strong>，整句在它的父段落上。
    const line = (await screen.findByText(/桥面系·桥面铺装/)).closest("p");
    expect(line).toHaveTextContent(
      "2025 年记录病害 4 条，为横向裂缝 3 条、网状裂缝 1 条；"
      + "2026 年记录 7 条，为横向裂缝 4 条、坑槽 2 条、网状裂缝 1 条。"
      + "较上年多 3 条，其中 1 个构件条数发生变化。");
  });

  // 增与减分开报，净值单列：一边多 5、一边少 5 不该说成"没变化"。
  it("summarises the added and removed counts separately", async () => {
    render(<MemoryRouter><BridgeOverviewPage /></MemoryRouter>);

    const summary = await screen.findByText(/^合计：/);
    expect(summary).toHaveTextContent("2025 年记录病害 4 条，2026 年 7 条");
    expect(summary).toHaveTextContent("增加 3 条、减少 0 条，净增 3 条");
    expect(summary).toHaveTextContent("2 个构件与上年持平");
    // 类型汇总必须含持平构件，否则"桥面铺装 2026 年 7 条"这句话本身就是错的。
    expect((await screen.findByText(/桥面系·桥面铺装/)).closest("p")).toHaveTextContent("3 个构件");
  });

  // 条数统计不等于病害身份匹配，口径必须写在界面上。
  it("states that the figures are counts rather than matched defects", async () => {
    render(<MemoryRouter><BridgeOverviewPage /></MemoryRouter>);

    expect(await screen.findByText(/不代表逐条病害的对应关系/)).toBeInTheDocument();
  });
});
