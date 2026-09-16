import { fireEvent, render, screen, within } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { ComponentSummary } from "../api/componentArchiveApi";
import { chooseOption } from "../test/antd";
import { ComponentListPanel } from "./ComponentListPanel";

function makeComponent(overrides: Partial<ComponentSummary> = {}): ComponentSummary {
  return {
    id: "component-1",
    system_number: "GJ-000001",
    structure_part: "上部结构",
    component_type: "板",
    business_component_code: "2-1#板",
    thread_count: 1,
    unbound_count: 0,
    first_seen_year: 2024,
    latest_seen_year: 2025,
    latest_score: 55.81,
    latest_score_year: 2025,
    ...overrides,
  };
}

const components = [
  makeComponent(),
  makeComponent({
    id: "component-2",
    system_number: "GJ-000002",
    structure_part: "桥面系",
    component_type: "伸缩缝",
    business_component_code: "伸缩缝装置",
    unbound_count: 2,
    first_seen_year: 2024,
    latest_seen_year: 2024,
    latest_score: null,
    latest_score_year: null,
  }),
  makeComponent({
    id: "component-3",
    system_number: "GJ-000003",
    business_component_code: "2-2#板",
    latest_score: 100,
  }),
];

/** jsdom 的 offsetTop/clientHeight 恒为 0，滚动逻辑得靠手工铺的布局来验证。 */
function stub(element: HTMLElement, metrics: Record<string, number>): void {
  for (const [name, value] of Object.entries(metrics)) {
    Object.defineProperty(element, name, { value, configurable: true });
  }
}

function groupHead(name: string): HTMLElement {
  return screen.getByRole("button", { name: new RegExp(name) });
}

/** 只取组内的构件行：分组标题本身也是 button，混进来会让行断言错位。 */
function groupItems(name: string): HTMLElement {
  return groupHead(name).parentElement?.querySelector("ul") as HTMLElement;
}

describe("ComponentListPanel", () => {
  // 一座桥几百个构件，平铺时每行长得一模一样。区分彼此的是编号而不是类型，
  // 所以类型收进分组标题，编号当主标题。
  it("groups components by structure part and type, with the code as the row title", () => {
    render(<ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    const plates = groupItems("上部结构 · 板");
    expect(within(plates).getByText("2-1#板")).toBeInTheDocument();
    expect(within(plates).getByText("2-2#板")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /桥面系 · 伸缩缝/ })).toBeInTheDocument();
  });

  // 组内最低分放在标题上：不展开也能看出哪一组藏着差构件。
  it("shows the count and the worst score on the group header", () => {
    render(<ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    const head = groupHead("上部结构 · 板");
    expect(head).toHaveTextContent("2");
    expect(head).toHaveTextContent("最低 55.81");
  });

  // 默认按评分升序：列表是用来找有问题的构件的，不是按编号翻字典。
  it("orders the worst score first by default", async () => {
    render(<ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    const codes = within(groupItems("上部结构 · 板"))
      .getAllByRole("button").map((item) => item.textContent);
    expect(codes[0]).toContain("2-1#板");
    expect(codes[1]).toContain("2-2#板");

    await chooseOption(screen.getByLabelText("排序方式"), "按编号");
    const byCode = within(groupItems("上部结构 · 板"))
      .getAllByRole("button").map((item) => item.textContent);
    expect(byCode[0]).toContain("2-1#板");
  });

  // "0 条待整理"是这屏的常态，逐行印一遍只会淹掉真正有待整理的那几个。
  it("marks pending observations only when there are any", () => {
    render(<ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    expect(within(groupItems("桥面系 · 伸缩缝")).getByText("2")).toBeInTheDocument();
    // 组标题上的合计与行上的标记是两回事，行里不该出现 0。
    expect(within(groupItems("上部结构 · 板")).queryByText("0")).not.toBeInTheDocument();
  });

  // 年度跨度不再逐行印：全桥常态占绝大多数，要看跨度点进档案页就是。
  it("does not print a year span on every row", () => {
    render(<ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    expect(screen.queryByText("2024–2025")).not.toBeInTheDocument();
    expect(screen.queryByText("仅 2024")).not.toBeInTheDocument();
  });

  // 去掉了"仅看待整理"开关，筛选只剩搜索与结构分部。
  it("no longer offers an unbound-only toggle", () => {
    render(<ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    expect(screen.queryByRole("checkbox")).not.toBeInTheDocument();
  });

  it("filters by keyword and structure part", async () => {
    render(<ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    fireEvent.change(screen.getByLabelText("搜索构件"), { target: { value: "伸缩" } });
    expect(screen.queryByRole("button", { name: /上部结构 · 板/ })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: /桥面系 · 伸缩缝/ })).toBeInTheDocument();

    fireEvent.change(screen.getByLabelText("搜索构件"), { target: { value: "" } });
    await chooseOption(screen.getByLabelText("结构分部筛选"), "上部结构");
    expect(screen.getByRole("button", { name: /上部结构 · 板/ })).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /桥面系 · 伸缩缝/ })).not.toBeInTheDocument();

    await chooseOption(screen.getByLabelText("结构分部筛选"), "全部结构");
    expect(screen.getByRole("button", { name: /上部结构 · 板/ })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /桥面系 · 伸缩缝/ })).toBeInTheDocument();
  });

  it("invokes onSelect with the component id and marks the active item", () => {
    const onSelect = vi.fn();
    render(
      <ComponentListPanel components={components} selectedComponentId="component-2" onSelect={onSelect} />);

    fireEvent.click(screen.getByText("2-1#板"));
    expect(onSelect).toHaveBeenCalledWith("component-1");
    expect(screen.getByText("伸缩缝装置").closest("button")).toHaveClass("active");
  });

  // 283 个"板"折起来，才轮得到看别的类型。
  it("collapses a group without losing the others", () => {
    render(<ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    fireEvent.click(screen.getByRole("button", { name: /上部结构 · 板/ }));

    expect(screen.queryByText("2-1#板")).not.toBeInTheDocument();
    expect(screen.getByText("伸缩缝装置")).toBeInTheDocument();
  });
  // 点击的是眼前看得见的那一行，界面不该动。深链接用的"滚到选中项"曾经无条件执行，
  // 表现就是"点哪儿，哪儿往上跳"。
  it("does not scroll when the clicked component is already visible", () => {
    const { container, rerender } = render(
      <ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    // jsdom 不做排版，所有尺寸都是 0；手工铺一份布局，效果里的判断才有东西可依据。
    const list = container.querySelector(".archive-component-list") as HTMLElement;
    stub(list, { offsetTop: 0, clientHeight: 200 });
    // 列表已经滚过一段，选中项落在当前视口之内（150 ≤ 150 < 300）。
    list.scrollTop = 100;
    const rows = [...container.querySelectorAll(".archive-group-items > li")] as HTMLElement[];
    rows.forEach((row) => stub(row, { offsetTop: 150, offsetHeight: 25 }));

    rerender(
      <ComponentListPanel components={components} selectedComponentId="component-1" onSelect={vi.fn()} />);

    // 旧逻辑会把它顶到列表最上面（scrollTop 变成 150），那正是"点哪儿哪儿往上跳"。
    expect(list.scrollTop).toBe(100);
  });

  // 但深链接进来时选中项可能在视野外，那时仍要把它露出来。
  it("scrolls a selection that sits below the viewport into view", () => {
    const { container, rerender } = render(
      <ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    const list = container.querySelector(".archive-component-list") as HTMLElement;
    stub(list, { offsetTop: 0, clientHeight: 60 });
    list.scrollTop = 0;
    const rows = [...container.querySelectorAll(".archive-group-items > li")] as HTMLElement[];
    // 把目标行放到远处，确保落在可视范围之外。
    rows.forEach((row) => stub(row, { offsetTop: 400, offsetHeight: 25 }));

    rerender(
      <ComponentListPanel components={components} selectedComponentId="component-1" onSelect={vi.fn()} />);

    // 只滚到刚好露出为止：底边对齐视口下沿。
    expect(list.scrollTop).toBe(400 + 25 - 60);
  });
});
