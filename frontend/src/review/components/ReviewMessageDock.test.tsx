import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { ReviewMessageDock } from "./ReviewMessageDock";

describe("ReviewMessageDock", () => {
  it("renders nothing when there is no message", () => {
    const { container } = render(<ReviewMessageDock saveMessage={null} onDismissSaveMessage={vi.fn()} />);
    expect(container).toBeEmptyDOMElement();
  });

  it("shows a success message without a close button", () => {
    render(
      <ReviewMessageDock saveMessage={{ kind: "success", text: "已保存" }} onDismissSaveMessage={vi.fn()} />
    );
    expect(screen.getByText("已保存")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "关闭消息" })).not.toBeInTheDocument();
  });

  it("shows an error message with issues and lets the user dismiss it", async () => {
    const onDismiss = vi.fn();
    render(
      <ReviewMessageDock
        saveMessage={{ kind: "error", text: "保存草稿失败", issues: [{ path: "defects[0].location", message: "位置不能为空" }] }}
       
        onDismissSaveMessage={onDismiss}
      />
    );
    expect(screen.getByText("保存草稿失败")).toBeInTheDocument();
    expect(screen.getByText("defects[0].location: 位置不能为空")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "关闭消息" }));
    expect(onDismiss).toHaveBeenCalledTimes(1);
  });

  it("shows a fallback instead of an empty error dock", () => {
    render(
      <ReviewMessageDock saveMessage={{ kind: "error", text: "   " }} onDismissSaveMessage={vi.fn()} />
    );

    expect(screen.getByText("操作失败，请稍后重试。")).toBeInTheDocument();
  });
});
