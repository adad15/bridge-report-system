import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { MemoryRouter, Route, Routes, useLocation } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchBridges, type BridgeSummary } from "../api/navigationApi";
import { chooseOption } from "../test/antd";
import { WorkbenchPage } from "./WorkbenchPage";

vi.mock("../api/navigationApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/navigationApi")>();
  return { ...original, fetchBridges: vi.fn() };
});

vi.mock("../auth/AuthContext", () => ({
  useAuth: () => ({ user: { username: "tester", display_name: "测试用户", role: "admin" } }),
}));

vi.mock("../workspace/CreateInspectionDialog", () => ({
  CreateInspectionDialog: ({ bridgeId }: { bridgeId: string }) => (
    <section aria-label="新建年度测试弹窗">{bridgeId}</section>
  ),
}));

const currentYear = new Date().getFullYear();

function bridge(overrides: Partial<BridgeSummary> & Pick<BridgeSummary, "id" | "bridge_name">): BridgeSummary {
  return {
    system_number: `QL-${overrides.id}`,
    route_name: "G305",
    status: "在用",
    bridge_scale: "大桥",
    latest_inspection_year: null,
    latest_overall_score: null,
    latest_overall_grade: null,
    pending_count: 0,
    ...overrides,
  };
}

function LocationProbe() {
  return <p aria-label="当前地址">{useLocation().pathname}</p>;
}

function renderPage() {
  return render(
    <MemoryRouter initialEntries={["/workbench"]}>
      <Routes>
        <Route path="/workbench" element={<WorkbenchPage />} />
        <Route path="*" element={<LocationProbe />} />
      </Routes>
    </MemoryRouter>
  );
}

describe("WorkbenchPage", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(fetchBridges).mockResolvedValue([
      bridge({ id: "b1", bridge_name: "百股大桥", latest_inspection_year: currentYear, latest_overall_score: 81.11, latest_overall_grade: "2类" }),
      bridge({ id: "b2", bridge_name: "大凌河特大桥", bridge_scale: "特大桥", latest_inspection_year: currentYear - 1, latest_overall_score: 68.4, latest_overall_grade: "3类", pending_count: 6 }),
      bridge({ id: "b3", bridge_name: "小凌河桥", latest_inspection_year: currentYear - 1, latest_overall_score: 55.8, latest_overall_grade: "4类", pending_count: 2 }),
      bridge({ id: "b4", bridge_name: "东沙河桥", bridge_scale: "中桥", pending_count: 3 }),
    ]);
  });

  it("summarises the bridge list into the four metrics", async () => {
    renderPage();
    const metrics = await screen.findByRole("group", { name: "工作概况" });
    expect(await within(metrics).findByText("特大桥 1 · 大桥 2 · 中桥 1")).toBeInTheDocument();
    expect(within(metrics).getByText("涉及 3 座桥梁")).toBeInTheDocument();
    expect(within(metrics).getByText("还有 3 座未完成本年度评定")).toBeInTheDocument();
    expect(within(metrics).getByText("其中 4、5 类 1 座")).toBeInTheDocument();
    expect(screen.getByText("测试用户", { exact: false })).toHaveTextContent("当前有 3 座桥有待处理事项。");
  });

  it("lists bridges with pending items from most to fewest and opens the bridge overview", async () => {
    renderPage();
    const region = await screen.findByRole("region", { name: "待处理的桥梁" });
    const names = await within(region).findAllByText(/大凌河特大桥|东沙河桥|小凌河桥/);
    expect(names.map((item) => item.textContent)).toEqual(["大凌河特大桥", "东沙河桥", "小凌河桥"]);
    expect(within(region).queryByText("百股大桥")).not.toBeInTheDocument();

    await userEvent.click(within(region).getByRole("button", { name: "去处理 东沙河桥" }));
    expect(await screen.findByLabelText("当前地址")).toHaveTextContent("/bridges/b4");
  });

  it("shows the grade distribution and the bridges rated 3 or worse, lowest score first", async () => {
    renderPage();
    const region = await screen.findByRole("region", { name: "技术状况等级分布" });
    expect(await within(region).findByLabelText("2类 1 座")).toBeInTheDocument();
    expect(within(region).getByLabelText("未评定 1 座")).toBeInTheDocument();
    const attention = within(region).getAllByText(/小凌河桥|大凌河特大桥/);
    expect(attention.map((item) => item.textContent)).toEqual(["小凌河桥", "大凌河特大桥"]);
  });

  it("asks for a bridge before opening the create inspection dialog", async () => {
    renderPage();
    await userEvent.click(await screen.findByRole("button", { name: /新建年度检测/ }));
    const dialog = await screen.findByRole("dialog", { name: "新建年度检测" });
    const next = within(dialog).getByRole("button", { name: "下一步" });
    expect(next).toBeDisabled();

    await chooseOption(within(dialog).getByLabelText("桥梁"), "东沙河桥（QL-b4）");
    await userEvent.click(next);
    expect(await screen.findByRole("region", { name: "新建年度测试弹窗" })).toHaveTextContent("b4");
  });
});
