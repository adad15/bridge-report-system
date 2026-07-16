import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchInspectionYears } from "../api/navigationApi";
import { fetchInspectionWorkspace } from "../api/workspaceApi";
import { InspectionWorkspacePage } from "./InspectionWorkspacePage";

const reloadOverview = vi.fn();

vi.mock("../api/navigationApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/navigationApi")>();
  return { ...original, fetchInspectionYears: vi.fn() };
});

vi.mock("../api/workspaceApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/workspaceApi")>();
  return { ...original, fetchInspectionWorkspace: vi.fn() };
});

vi.mock("../auth/AuthContext", () => ({
  useAuth: () => ({ user: { username: "admin", display_name: "管理员", role: "admin" } }),
}));

vi.mock("../workspace/BridgeWorkspaceShell", () => ({
  useBridgeWorkspace: () => ({
    overview: { bridge: { id: "bridge-1", bridge_name: "百股大桥" } },
    reloadOverview,
  }),
}));

vi.mock("../workspace/ImportWordDialog", () => ({
  ImportWordDialog: ({ onChanged }: { onChanged: () => void }) => (
    <section aria-label="导入测试弹窗">
      <button type="button" onClick={onChanged}>模拟上传后刷新</button>
    </section>
  ),
}));

describe("InspectionWorkspacePage", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchInspectionYears).mockResolvedValue([{
      id: "year-1", system_number: "NDJC-000001", inspection_year: 2024,
      status: "待校对", version_number: 1, is_current: true,
    }]);
    vi.mocked(fetchInspectionWorkspace).mockResolvedValue({
      bridge: { id: "bridge-1", system_number: "QL-000001", bridge_name: "百股大桥", route_name: "大养线", status: "在用" },
      inspection_year: { id: "year-1", system_number: "NDJC-000001", inspection_year: 2024, status: "待校对", version_number: 1, is_current: true, overall_score: null, overall_grade: null, created_at: null, updated_at: null },
      imports: [],
      pending: { import_count: 0, unbound_observation_count: 0, total_count: 0 },
    });
  });

  it("keeps the import dialog mounted while refreshing the annual workspace", async () => {
    render(
      <MemoryRouter initialEntries={["/bridges/bridge-1/inspections/year-1"]}>
        <Routes>
          <Route path="/bridges/:bridgeId/inspections/:inspectionYearId" element={<InspectionWorkspacePage />} />
        </Routes>
      </MemoryRouter>
    );

    await userEvent.click(await screen.findByRole("button", { name: "导入资料" }));
    expect(screen.getByRole("region", { name: "导入测试弹窗" })).toBeInTheDocument();

    vi.mocked(fetchInspectionWorkspace).mockImplementation(() => new Promise(() => undefined));
    await userEvent.click(screen.getByRole("button", { name: "模拟上传后刷新" }));

    expect(screen.getByRole("region", { name: "导入测试弹窗" })).toBeInTheDocument();
    expect(reloadOverview).not.toHaveBeenCalled();
  });
});
