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
    // 未传重开入口时不渲染对应按钮。
    expect(screen.queryByRole("button", { name: "修正警告病害" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "解锁全部修改" })).not.toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "返回桥梁详情" }));
    expect(onBack).toHaveBeenCalledTimes(1);
  });

  it("offers reopen entries on the read-only bar when handlers are provided", async () => {
    const onReopenWarnings = vi.fn();
    const onReopenFull = vi.fn();
    render(
      <ReviewActionBar
        readOnlyNotice="本导入记录已确认入库，页面转为只读。"
        onReopenWarnings={onReopenWarnings}
        onReopenFull={onReopenFull}
      />
    );

    await userEvent.click(screen.getByRole("button", { name: "修正警告病害" }));
    await userEvent.click(screen.getByRole("button", { name: "解锁全部修改" }));
    expect(onReopenWarnings).toHaveBeenCalledTimes(1);
    expect(onReopenFull).toHaveBeenCalledTimes(1);
  });

  it("swaps cancel for abandon-reopen while a reopen session is active", async () => {
    const onAbandonReopen = vi.fn();
    render(<ReviewActionBar onAbandonReopen={onAbandonReopen} />);

    expect(screen.queryByRole("button", { name: "取消导入" })).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "放弃修改" }));
    expect(onAbandonReopen).toHaveBeenCalledTimes(1);
  });

  it("uses the annual workspace return label when supplied", async () => {
    const onBack = vi.fn();
    render(<ReviewActionBar onBackToBridge={onBack} backLabel="返回 2026 年度工作台" />);
    await userEvent.click(screen.getByRole("button", { name: "返回 2026 年度工作台" }));
    expect(onBack).toHaveBeenCalledTimes(1);
  });
});
