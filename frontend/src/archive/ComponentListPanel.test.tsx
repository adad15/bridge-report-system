import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { ComponentSummary } from "../api/componentArchiveApi";
import { ComponentListPanel } from "./ComponentListPanel";

function makeComponent(overrides: Partial<ComponentSummary> = {}): ComponentSummary {
  return {
    id: "component-1",
    system_number: "GJ-000001",
    structure_part: "上部结构",
    component_type: "2-1#板",
    business_component_code: "上部承重构件",
    thread_count: 1,
    unbound_count: 0,
    first_seen_year: 2024,
    latest_seen_year: 2025,
    latest_score: 55.81,
    latest_score_year: 2025,
    ...overrides,
  };
}

describe("ComponentListPanel", () => {
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
  ];

  it("renders every component with thread/unbound counts and year span", () => {
    render(<ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    expect(screen.getByText("2-1#板")).toBeInTheDocument();
    expect(screen.getByText("伸缩缝")).toBeInTheDocument();
    expect(screen.getByText("2024-2025")).toBeInTheDocument();
    expect(screen.getByText("2024")).toBeInTheDocument();
    expect(screen.getByText("评分 55.81")).toBeInTheDocument();
  });

  it("filters by keyword, structure part, and unbound-only toggle", () => {
    render(<ComponentListPanel components={components} selectedComponentId={null} onSelect={vi.fn()} />);

    fireEvent.change(screen.getByLabelText("搜索构件"), { target: { value: "伸缩" } });
    expect(screen.queryByText("2-1#板")).not.toBeInTheDocument();
    expect(screen.getByText("伸缩缝")).toBeInTheDocument();

    fireEvent.change(screen.getByLabelText("搜索构件"), { target: { value: "" } });
    fireEvent.change(screen.getByLabelText("结构分部筛选"), { target: { value: "上部结构" } });
    expect(screen.getByText("2-1#板")).toBeInTheDocument();
    expect(screen.queryByText("伸缩缝")).not.toBeInTheDocument();

    fireEvent.change(screen.getByLabelText("结构分部筛选"), { target: { value: "全部" } });
    fireEvent.click(screen.getByRole("checkbox"));
    expect(screen.queryByText("2-1#板")).not.toBeInTheDocument();
    expect(screen.getByText("伸缩缝")).toBeInTheDocument();
  });

  it("invokes onSelect with the component id and marks the active item", () => {
    const onSelect = vi.fn();
    render(<ComponentListPanel components={components} selectedComponentId="component-2" onSelect={onSelect} />);

    fireEvent.click(screen.getByText("2-1#板"));
    expect(onSelect).toHaveBeenCalledWith("component-1");
    expect(screen.getByText("伸缩缝").closest("button")).toHaveClass("active");
  });
});
