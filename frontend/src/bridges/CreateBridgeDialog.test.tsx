import { useEffect } from "react";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { createBridge } from "../api/bridgeAdministrationApi";
import { generateComponentInventory } from "../api/componentInventoryApi";
import { CreateBridgeDialog } from "./CreateBridgeDialog";

vi.mock("../api/bridgeAdministrationApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/bridgeAdministrationApi")>();
  return { ...original, createBridge: vi.fn() };
});

vi.mock("../api/componentInventoryApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/componentInventoryApi")>();
  return { ...original, generateComponentInventory: vi.fn() };
});

const plan = {
  standard_package_id: "package-1", bridge_type_id: "h21.bridge_type.beam", span_count: 1,
  part_selections: [{ part_key: "beam.girder", site_name: "主梁", counts: [1] }],
};

vi.mock("./BridgeInventoryWizard", () => ({
  BridgeInventoryWizard: ({ onPlanChange }: { onPlanChange: (value: typeof plan) => void }) => {
    useEffect(() => onPlanChange(plan), [onPlanChange]);
    return <div>台账配置就绪</div>;
  },
}));

describe("CreateBridgeDialog", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(createBridge).mockResolvedValue({
      id: "bridge-1", system_number: "QL-000001", bridge_name: "测试桥", route_number: null,
      route_name: null, administrative_region: null, station_mark: null, status: "在用",
    });
    vi.mocked(generateComponentInventory).mockResolvedValue({
      id: "revision-1", bridge_id: "bridge-1", revision_number: 1, status: "draft",
      baseline_revision_id: null, confirmed_at: null, entries: [],
    });
  });

  it("creates the bridge and its draft inventory in order", async () => {
    const onCreated = vi.fn();
    render(<CreateBridgeDialog onClose={vi.fn()} onCreated={onCreated} />);
    await userEvent.type(screen.getByLabelText("桥梁名称"), "测试桥");
    await userEvent.click(screen.getByRole("button", { name: "下一步：构件台账" }));
    expect(await screen.findByText("台账配置就绪")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "创建桥梁并生成台账" }));

    expect(createBridge).toHaveBeenCalledWith(expect.any(String), expect.objectContaining({ bridge_name: "测试桥" }));
    expect(generateComponentInventory).toHaveBeenCalledWith(expect.any(String), "bridge-1", plan);
    expect(onCreated).toHaveBeenCalledWith(expect.objectContaining({ id: "bridge-1" }));
  });
});
