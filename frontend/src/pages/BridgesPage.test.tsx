import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { MemoryRouter } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchBridges } from "../api/navigationApi";
import { fetchStandardPackages } from "../api/standardsApi";
import { useAuth } from "../auth/AuthContext";
import { BridgesPage } from "./BridgesPage";

vi.mock("../api/navigationApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/navigationApi")>();
  return { ...original, fetchBridges: vi.fn() };
});

vi.mock("../auth/AuthContext", () => ({
  useAuth: vi.fn(),
}));

vi.mock("../api/standardsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardsApi")>();
  return { ...original, fetchStandardPackages: vi.fn() };
});

describe("BridgesPage", () => {
  beforeEach(() => {
    vi.mocked(useAuth).mockReturnValue({ user: { username: "admin", display_name: "管理员", role: "admin" } } as never);
    vi.mocked(fetchStandardPackages).mockResolvedValue([]);
    vi.mocked(fetchBridges).mockResolvedValue([
      { id: "b1", system_number: "QL-000001", bridge_name: "绕阳河二号桥", route_name: "G305", status: "在用", latest_inspection_year: 2026, latest_overall_score: 85.61, latest_overall_grade: "2类", pending_count: 2 },
      { id: "b2", system_number: "QL-000002", bridge_name: "测试桥", route_name: "S101", status: "在用", latest_inspection_year: null, latest_overall_score: null, latest_overall_grade: null, pending_count: 0 },
    ]);
  });

  it("shows standards management only to administrators", async () => {
    const { unmount } = render(<MemoryRouter><BridgesPage /></MemoryRouter>);
    expect(await screen.findByRole("button", { name: "规范管理" })).toBeInTheDocument();
    unmount();

    vi.mocked(useAuth).mockReturnValue({ user: { username: "normal", display_name: "普通用户", role: "normal" } } as never);
    render(<MemoryRouter><BridgesPage /></MemoryRouter>);
    await screen.findByText("绕阳河二号桥");
    expect(screen.queryByRole("button", { name: "规范管理" })).not.toBeInTheDocument();
  });

  it("shows archive summaries and filters by bridge name, number, or route", async () => {
    render(<MemoryRouter><BridgesPage /></MemoryRouter>);
    expect(await screen.findByText("绕阳河二号桥")).toBeInTheDocument();
    expect(screen.getByText("2026 · 2类")).toBeInTheDocument();
    const search = screen.getByRole("searchbox", { name: /搜索桥名/ });
    await userEvent.type(search, "S101");
    expect(screen.queryByText("绕阳河二号桥")).not.toBeInTheDocument();
    expect(screen.getByText("测试桥")).toBeInTheDocument();
  });

  it("selects bridges without navigating and clears selection when search changes", async () => {
    render(<MemoryRouter><BridgesPage /></MemoryRouter>);
    await screen.findByText("绕阳河二号桥");
    const remove = screen.getByRole("button", { name: "删除选中桥梁（0）" });
    expect(remove).toBeDisabled();
    await userEvent.click(screen.getByRole("checkbox", { name: "选择 QL-000001" }));
    expect(screen.getByRole("button", { name: "删除选中桥梁（1）" })).toBeEnabled();
    await userEvent.type(screen.getByRole("searchbox", { name: /搜索桥名/ }), "测试");
    expect(screen.getByRole("button", { name: "删除选中桥梁（0）" })).toBeDisabled();
  });
});
