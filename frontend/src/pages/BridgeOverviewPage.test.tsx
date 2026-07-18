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
  it("renders the component inventory for the current workspace bridge", () => {
    render(<MemoryRouter><BridgeOverviewPage /></MemoryRouter>);
    expect(screen.getByText("台账桥梁：bridge-1")).toBeInTheDocument();
  });
});
