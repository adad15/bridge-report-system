import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { Link, useParams } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";
import type { ReactNode } from "react";
import { App } from "./App";

const { auth, imported, overview } = vi.hoisted(() => ({
  auth: { user: null as { username: string; display_name: string; role: "admin" } | null, restoring: false },
  imported: vi.fn(),
  overview: vi.fn(),
}));
vi.mock("./auth/AuthContext", () => ({
  AuthProvider: ({ children }: { children: ReactNode }) => <>{children}</>,
  useAuth: () => ({ ...auth, login: vi.fn(), logout: vi.fn() }),
}));
vi.mock("./api/workspaceApi", async (original) => ({
  ...await original<typeof import("./api/workspaceApi")>(),
  fetchBridgeOverview: overview,
}));
vi.mock("./pages/LoginPage", () => ({
  LoginPage: () => <p>登录入口</p>,
  LoginRestoringPage: () => <p>正在恢复会话</p>,
}));
vi.mock("./pages/WorkbenchPage", () => {
  imported("workbench");
  return { WorkbenchPage: () => <p>工作台内容</p> };
});
vi.mock("./pages/BridgeOverviewPage", () => {
  imported("overview");
  return { BridgeOverviewPage: () => <Link to="components">打开构件页</Link> };
});
vi.mock("./pages/ComponentArchivePage", () => ({
  ComponentArchivePage: () => <p>构件页内容</p>,
}));
vi.mock("./pages/ThreadTriagePage", () => ({
  ThreadTriagePage: () => <p>线索整理内容</p>,
}));
vi.mock("./pages/ReportGenerationPage", () => ({
  ReportGenerationPage: () => <p>报告年度：{useParams().inspectionYearId}</p>,
}));
vi.mock("./pages/ReviewWorkspacePage", () => {
  imported("review");
  return { ReviewWorkspacePage: () => <p>校对：{useParams().importRecordId}</p> };
});

function visit(path: string) {
  window.history.replaceState({}, "", path);
  return render(<App />);
}

beforeEach(() => {
  auth.user = { username: "tester", display_name: "测试用户", role: "admin" };
  auth.restoring = false;
  overview.mockReset().mockResolvedValue({
    bridge: { id: "b1", system_number: "QL-1", bridge_name: "测试桥", route_name: null, status: "在用" },
  });
});

describe("lazy application routes", () => {
  it("keeps login and restoration available without importing business pages", () => {
    auth.user = null;
    const view = visit("/workbench");
    expect(screen.getByText("登录入口")).toBeInTheDocument();
    expect(imported).not.toHaveBeenCalled();
    auth.restoring = true;
    view.rerender(<App />);
    expect(screen.getByText("正在恢复会话")).toBeInTheDocument();
    expect(imported).not.toHaveBeenCalled();
  });
  it("loads the workbench after login without loading the review workspace", async () => {
    visit("/workbench");
    await screen.findByText("工作台内容");
    expect(imported).toHaveBeenCalledWith("workbench");
    expect(imported).not.toHaveBeenCalledWith("review");
  });
  it("retains the bridge shell and overview when switching child routes", async () => {
    visit("/bridges/b1");
    fireEvent.click(await screen.findByRole("link", { name: "打开构件页" }));
    await screen.findByText("构件页内容");
    expect(overview).toHaveBeenCalledTimes(1);
    expect(screen.getByText("测试桥")).toBeInTheDocument();
  });
  it("keeps the legacy triage redirect working inside the bridge shell", async () => {
    visit("/bridges/b1/defect-threads/review");
    await screen.findByText("线索整理内容");
    await waitFor(() => expect(window.location.pathname).toBe("/bridges/b1/defect-threads/triage"));
  });
  it("supports a direct report URL with the correct inspection year", async () => {
    visit("/bridges/b1/inspections/year-2026/report");
    await screen.findByText("报告年度：year-2026");
  });
  it("supports a direct review URL with the full-screen workspace shell", async () => {
    visit("/bridges/b1/inspections/y1/imports/import-7/review");
    await screen.findByText("校对：import-7");
    expect(overview).not.toHaveBeenCalled();
  });
});
