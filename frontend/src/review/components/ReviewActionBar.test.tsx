import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { ReviewActionBar } from "./ReviewActionBar";

describe("ReviewActionBar", () => {
  it("disables every action button when no handler is provided", () => {
    render(<ReviewActionBar />);
    screen.getAllByRole("button").forEach((button) => expect(button).toBeDisabled());
    expect(screen.queryByText(/有未保存的修改/)).not.toBeInTheDocument();
  });

  it("shows the unsaved-changes hint when dirty", () => {
    render(<ReviewActionBar dirty />);
    expect(screen.getByText(/有未保存的修改/)).toBeInTheDocument();
  });

  it("replaces the actions with a read-only notice and back button", async () => {
    const onBack = vi.fn();
    render(<ReviewActionBar readOnlyNotice="本导入记录已确认入库，页面转为只读。" onBackToBridge={onBack} />);

    expect(screen.getByText("本导入记录已确认入库，页面转为只读。")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "保存草稿" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "确认年度事实入库" })).not.toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "返回桥梁详情" }));
    expect(onBack).toHaveBeenCalledTimes(1);
  });
});
