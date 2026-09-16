import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { MemoryRouter } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { emulateViewport } from "../test/antd";
import { AppLayout } from "./AppLayout";
import { useHeaderBridge } from "./HeaderBridgeContext";

const authState = vi.hoisted(() => ({ role: "admin", logout: vi.fn() }));

vi.mock("../auth/AuthContext", () => ({
  useAuth: () => ({
    user: { username: "tester", display_name: "测试用户", role: authState.role },
    logout: authState.logout,
  }),
}));

function renderAt(path: string) {
  return render(
    <MemoryRouter initialEntries={[path]}>
      <AppLayout>
        <p>页面内容</p>
      </AppLayout>
    </MemoryRouter>
  );
}

/** 模拟桥梁工作区外壳：挂载后把当前桥梁报给顶栏。 */
function BridgePage() {
  useHeaderBridge({ id: "bridge-1", name: "百股大桥", status: "在用", systemNumber: "QL-000014", routeName: "大养线" });
  return <p>桥梁页面</p>;
}

function navigation() {
  return screen.getByRole("complementary", { name: "主导航" });
}

describe("AppLayout", () => {
  beforeEach(() => {
    authState.role = "admin";
    authState.logout.mockReset();
    emulateViewport(1440);
  });

  it("renders the page inside the shell", () => {
    renderAt("/workbench");

    expect(screen.getByText("页面内容")).toBeInTheDocument();
    expect(screen.getByRole("link", { name: "桥梁检测报告系统工作台" })).toHaveAttribute("href", "/workbench");
  });

  // 系统管理那一组只给管理员（设计 §22）；隐藏入口不等于权限，后端另有校验。
  it("shows the system group to administrators only", () => {
    renderAt("/workbench");
    expect(within(navigation()).getByRole("link", { name: "报告模板" })).toBeInTheDocument();

    authState.role = "normal";
    renderAt("/workbench");
    const lists = screen.getAllByRole("complementary", { name: "主导航" });
    expect(within(lists[1]).queryByRole("link", { name: "报告模板" })).not.toBeInTheDocument();
    expect(within(lists[1]).getByRole("link", { name: "桥梁档案" })).toBeInTheDocument();
  });

  // 规范管理挂在桥梁档案页的查询串上，两项不能同时亮。
  it("highlights exactly one navigation entry", () => {
    renderAt("/bridges?standards=1");

    const selected = within(navigation())
      .getAllByRole("menuitem")
      .filter((item) => item.classList.contains("ant-menu-item-selected"));
    expect(selected).toHaveLength(1);
    expect(selected[0]).toHaveTextContent("规范管理");
  });

  it("switches the header to the bridge workspace tabs inside a bridge", () => {
    renderAt("/bridges/bridge-1/inspections/year-1");

    const tabs = screen.getByRole("menu", { name: "桥梁工作区" });
    expect(within(tabs).getByRole("link", { name: "桥梁概览" })).toHaveAttribute("href", "/bridges/bridge-1");
    const selected = within(tabs)
      .getAllByRole("menuitem")
      .filter((item) => item.classList.contains("ant-menu-item-selected"));
    expect(selected.map((item) => item.textContent)).toEqual(["年度检测"]);
  });

  // 桥名、状态、编号挂在顶栏的桥梁页签前面，页面里不再单占一张卡片。
  it("shows the current bridge in the header with a way back to the list", async () => {
    render(
      <MemoryRouter initialEntries={["/bridges/bridge-1"]}>
        <AppLayout>
          <BridgePage />
        </AppLayout>
      </MemoryRouter>
    );

    const header = screen.getByRole("banner");
    expect(await within(header).findByRole("heading", { name: "百股大桥" })).toBeInTheDocument();
    expect(within(header).getByText("在用")).toBeInTheDocument();
    expect(within(header).getByText("QL-000014 · 大养线")).toBeInTheDocument();
    expect(within(header).getByRole("button", { name: "返回桥梁档案" })).toBeInTheDocument();
  });

  // 笔记本屏幕上顶栏最挤，编号和线路先让位；桥名、状态和返回箭头留着。
  it("drops the bridge number on a narrower screen", async () => {
    emulateViewport(1280);
    render(
      <MemoryRouter initialEntries={["/bridges/bridge-1"]}>
        <AppLayout>
          <BridgePage />
        </AppLayout>
      </MemoryRouter>
    );

    const header = screen.getByRole("banner");
    expect(await within(header).findByRole("heading", { name: "百股大桥" })).toBeInTheDocument();
    expect(within(header).queryByText("QL-000014 · 大养线")).not.toBeInTheDocument();
    expect(within(header).getByRole("button", { name: "返回桥梁档案" })).toBeInTheDocument();
  });

  it("collapses the sidebar and hides the group titles", async () => {
    renderAt("/workbench");
    expect(screen.getByText("业务中心")).toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "收起侧边栏" }));

    expect(screen.getByRole("button", { name: "展开侧边栏" })).toHaveAttribute("aria-expanded", "false");
    expect(screen.queryByText("业务中心")).not.toBeInTheDocument();
  });

  // 窄于 992px 的屏幕：侧栏固定收起，也不给展开按钮。
  it("keeps the sidebar collapsed on a narrow screen", () => {
    emulateViewport(900);
    renderAt("/workbench");

    expect(screen.queryByRole("button", { name: /侧边栏/ })).not.toBeInTheDocument();
    expect(screen.queryByText("业务中心")).not.toBeInTheDocument();
  });

  it("logs out from the header", async () => {
    renderAt("/workbench");

    await userEvent.click(screen.getByRole("button", { name: /退出登录/ }));

    expect(authState.logout).toHaveBeenCalledTimes(1);
  });
});
