import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import type { PreflightResponse } from "../../api/reviewApi";
import { ReviewMessageDock } from "./ReviewMessageDock";

function blockedPreflight(): PreflightResponse {
  return {
    can_confirm: false,
    requires_revision_confirmation: false,
    blocking_errors: [
      { code: "defect_unconfirmed", message: "仍有未确认的病害候选", target_candidate_id: "defect_0013" },
      { code: "photo_unresolved", message: "仍有未处理的照片候选", target_candidate_id: null },
    ],
    warnings: [{ code: "rating_missing_note", message: "评分缺少备注", target_candidate_id: null }],
  };
}

describe("ReviewMessageDock", () => {
  it("renders nothing when there is no message and no preflight result", () => {
    const { container } = render(<ReviewMessageDock saveMessage={null} preflight={null} onDismissSaveMessage={vi.fn()} />);
    expect(container).toBeEmptyDOMElement();
  });

  it("shows a success message without a close button", () => {
    render(
      <ReviewMessageDock saveMessage={{ kind: "success", text: "已保存" }} preflight={null} onDismissSaveMessage={vi.fn()} />
    );
    expect(screen.getByText("已保存")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "关闭消息" })).not.toBeInTheDocument();
  });

  it("shows an error message with issues and lets the user dismiss it", async () => {
    const onDismiss = vi.fn();
    render(
      <ReviewMessageDock
        saveMessage={{ kind: "error", text: "保存草稿失败", issues: [{ path: "defects[0].location", message: "位置不能为空" }] }}
        preflight={null}
        onDismissSaveMessage={onDismiss}
      />
    );
    expect(screen.getByText("保存草稿失败")).toBeInTheDocument();
    expect(screen.getByText("defects[0].location: 位置不能为空")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "关闭消息" }));
    expect(onDismiss).toHaveBeenCalledTimes(1);
  });

  it("summarizes a blocked preflight and expands to the issue list on demand", async () => {
    render(<ReviewMessageDock saveMessage={null} preflight={blockedPreflight()} onDismissSaveMessage={vi.fn()} />);

    expect(screen.getByText("入库前检查未通过：2 个阻断项、1 个警告。")).toBeInTheDocument();
    expect(screen.queryByText(/defect_unconfirmed/)).not.toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "展开 ▾" }));
    expect(screen.getByText("defect_unconfirmed：仍有未确认的病害候选（defect_0013）")).toBeInTheDocument();
    expect(screen.getByText("photo_unresolved：仍有未处理的照片候选")).toBeInTheDocument();
    expect(screen.getByText("rating_missing_note：评分缺少备注")).toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "收起 ▴" }));
    expect(screen.queryByText(/defect_unconfirmed/)).not.toBeInTheDocument();
  });

  it("shows a passing preflight without an expand button when there are no warnings", () => {
    const preflight: PreflightResponse = {
      can_confirm: true,
      requires_revision_confirmation: false,
      blocking_errors: [],
      warnings: [],
    };
    render(<ReviewMessageDock saveMessage={null} preflight={preflight} onDismissSaveMessage={vi.fn()} />);
    expect(screen.getByText("检查通过，可以确认入库。")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "展开 ▾" })).not.toBeInTheDocument();
  });
});
