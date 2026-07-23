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
    },
    reloadOverview: vi.fn(),
  }),
}));

vi.mock("../bridges/ComponentInventoryEditor", () => ({
  ComponentInventoryEditor: ({ bridgeId }: { bridgeId: string }) => <div>台账桥梁：{bridgeId}</div>,
}));

describe("BridgeOverviewPage", () => {
  // 台账构件可达数千条，挂在总览页会让每次进桥、每次切回都先等它整份加载完。
  // 台账在 /bridges/:id/inventory，入口由工作区标签导航提供，总览页不碰它。
  it("does not load the inventory", () => {
    render(<MemoryRouter><BridgeOverviewPage /></MemoryRouter>);
    expect(screen.queryByText("台账桥梁：bridge-1")).not.toBeInTheDocument();
  });
});
