import { render, screen } from "@testing-library/react";
import { MemoryRouter } from "react-router-dom";
import { describe, expect, it, vi } from "vitest";

import { ComponentInventoryPage } from "./ComponentInventoryPage";

vi.mock("../workspace/BridgeWorkspaceShell", () => ({
  useBridgeWorkspace: () => ({
    overview: { bridge: { id: "bridge-1", bridge_name: "测试桥" } },
    reloadOverview: vi.fn(),
  }),
}));

vi.mock("../bridges/ComponentInventoryEditor", () => ({
  ComponentInventoryEditor: ({ bridgeId }: { bridgeId: string }) => <div>台账桥梁：{bridgeId}</div>,
}));

describe("ComponentInventoryPage", () => {
  it("renders the inventory for the current workspace bridge", () => {
    render(<MemoryRouter><ComponentInventoryPage /></MemoryRouter>);
    expect(screen.getByText("台账桥梁：bridge-1")).toBeInTheDocument();
  });
});
