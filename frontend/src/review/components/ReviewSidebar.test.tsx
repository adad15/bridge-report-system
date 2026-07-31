import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import type { ReviewCounts } from "../grouping";
import { ReviewSidebar } from "./ReviewSidebar";

const counts: ReviewCounts = {
  defect_count: 279,
  photo_count: 166,
  rating_item_count: 1,
  pending_count: 0,
  confirmed_count: 0,
  modified_count: 0,
  ignored_count: 0,
  object_warning_count: 0,
};

describe("ReviewSidebar", () => {
  it("puts 构件绑定 first, ahead of 病害与照片", () => {
    render(
      <ReviewSidebar counts={counts} bindingPendingCount={46} active="component_binding" onSelect={vi.fn()} />
    );
    const labels = screen.getAllByRole("button").map((item) => item.textContent);
    expect(labels).toEqual([
      "构件绑定46",
      "病害与照片279",
      "系统技术状况评定1",
      "原始 JSON-",
    ]);
  });

  // 台账未确认时无法绑定，计数为 null；此时必须显示 "-"，显示 0 会被读成"都处理完了"。
  it("shows a dash rather than zero when binding is unavailable", () => {
    render(
      <ReviewSidebar counts={counts} bindingPendingCount={null} active="component_binding" onSelect={vi.fn()} />
    );
    expect(screen.getByRole("button", { name: /构件绑定/ })).toHaveTextContent("构件绑定-");
  });

  it("reports the selected group", async () => {
    const onSelect = vi.fn();
    render(
      <ReviewSidebar counts={counts} bindingPendingCount={0} active="component_binding" onSelect={onSelect} />
    );
    await userEvent.click(screen.getByRole("button", { name: /构件绑定/ }));
    expect(onSelect).toHaveBeenCalledWith("component_binding");
  });
});
