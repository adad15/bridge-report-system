import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import type { PreflightResponse } from "../../api/reviewApi";
import { ReviewActionBar } from "./ReviewActionBar";

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

  it("summarizes a blocked preflight and expands to the issue list on demand", async () => {
    render(<ReviewActionBar preflight={blockedPreflight()} />);

    expect(screen.getByText("入库前检查：2 个阻断项 · 1 个提醒")).toBeInTheDocument();
    expect(screen.queryByText(/defect_unconfirmed/)).not.toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "展开入库前检查明细" }));
    expect(screen.getByText("defect_unconfirmed：仍有未确认的病害候选（defect_0013）")).toBeInTheDocument();
    expect(screen.getByText("photo_unresolved：仍有未处理的照片候选")).toBeInTheDocument();
    expect(screen.getByText("rating_missing_note：评分缺少备注")).toBeInTheDocument();

    await userEvent.click(screen.getByRole("button", { name: "收起入库前检查明细" }));
    expect(screen.queryByText(/defect_unconfirmed/)).not.toBeInTheDocument();
  });

  it("translates internal candidate ids, issue codes, and field names into business labels", async () => {
    const preflight: PreflightResponse = {
      can_confirm: false,
      requires_revision_confirmation: false,
      blocking_errors: [{
        code: "defect_missing_required_field",
        message: "已确认病害 source_defect_0174 缺少必填字段 defect_type。",
        target_candidate_id: "source_defect_0174",
      }],
      warnings: [{
        code: "defect_location_missing",
        message: "病害 source_defect_0006 未记录详细位置，仍可入库；后续跨年病害匹配精度可能降低。",
        target_candidate_id: "source_defect_0006",
      }],
    };
    const targetLabels = new Map([
      ["source_defect_0174", "【2-1#板～2-25#板｜存在熏黑痕迹｜板底】"],
      ["source_defect_0006", "【1-1#板｜渗水泛碱】"],
    ]);

    render(
      <ReviewActionBar
        preflight={preflight}
        preflightTargetLabels={targetLabels}
      />
    );
    await userEvent.click(screen.getByRole("button", { name: "展开入库前检查明细" }));

    expect(screen.getByText("必填信息缺失：已确认病害 【2-1#板～2-25#板｜存在熏黑痕迹｜板底】 缺少必填字段 “病害类型”。")).toBeInTheDocument();
    expect(screen.getByText("详细位置缺失：病害 【1-1#板｜渗水泛碱】 未记录详细位置，仍可入库；后续跨年病害匹配精度可能降低。")).toBeInTheDocument();
    expect(screen.queryByText(/source_defect_/)).not.toBeInTheDocument();
    expect(screen.queryByText(/defect_type/)).not.toBeInTheDocument();
  });

  it("shows a passing preflight without an expand button when there are no warnings", () => {
    const preflight: PreflightResponse = {
      can_confirm: true,
      requires_revision_confirmation: false,
      blocking_errors: [],
      warnings: [],
    };
    render(<ReviewActionBar preflight={preflight} />);
    expect(screen.getByText("检查通过，可以确认入库。")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "展开入库前检查明细" })).not.toBeInTheDocument();
  });
});
