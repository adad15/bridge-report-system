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
  needs_attention_count: 333,
};

describe("ReviewSidebar", () => {
  it("puts 构件绑定 between 需要处理 and 病害与照片", () => {
    render(<ReviewSidebar counts={counts} active="needs_attention" onSelect={vi.fn()} />);
    const labels = screen.getAllByRole("button").map((item) => item.textContent);
    expect(labels).toEqual([
      "需要处理333",
      "构件绑定-",  // 绑定进度由分区自己拉取，不在 ReviewCounts 里
      "病害与照片279",
      "系统技术状况评定1",
      "原始 JSON-",
    ]);
  });

  it("reports the selected group", async () => {
    const onSelect = vi.fn();
    render(<ReviewSidebar counts={counts} active="needs_attention" onSelect={onSelect} />);
    await userEvent.click(screen.getByRole("button", { name: /构件绑定/ }));
    expect(onSelect).toHaveBeenCalledWith("component_binding");
  });
});
