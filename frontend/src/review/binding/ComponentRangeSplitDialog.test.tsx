import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { ComponentRangeSplitDialog } from "./ComponentRangeSplitDialog";
import type { ResolutionPlanPreview, ResolutionPlanRow } from "../../api/resolutionApi";

// 5.0：展开计划由后端生成，前端只展示并拿 plan token 去执行。这些用例因此不验证
// "前端算得对不对"，而验证"后端给什么就显示什么、应用时提交的是 token"。

function planRow(overrides: Partial<ResolutionPlanRow> = {}): ResolutionPlanRow {
  return {
    group_id: `group-${overrides.source_component_number ?? "x"}`,
    source_component_name: "上部承重构件",
    source_component_number: "1-1#板~1-25#板",
    member_count: 3,
    resolved_numbers: [],
    target_component_ids: Array.from({ length: 25 }, (_, i) => `c${i}`),
    outcome: "will_bind",
    reason_code: "",
    reason_message: "",
    ...overrides,
  };
}

function plan(overrides: Partial<ResolutionPlanPreview> = {}): ResolutionPlanPreview {
  return {
    plan_token: "22222222-2222-4222-8222-222222222222",
    operation_type: "range_expand",
    expires_at: "2026-08-28T10:15:00+08:00",
    will_apply_count: 1,
    skipped_count: 1,
    blocked_count: 0,
    instances_before: 4,
    instances_after: 28,
    rating_recomputed_count: 24,
    inventory_revision_id: "revision-1",
    rating_tree_version_id: "tree-1",
    rows: [
      planRow(),
      planRow({
        source_component_number: "3#板",
        target_component_ids: [],
        outcome: "skipped",
        reason_code: "not_a_range",
        reason_message: "该编号不是可展开的构件范围。",
      }),
    ],
    ...overrides,
  };
}

function renderDialog(overrides: Partial<Parameters<typeof ComponentRangeSplitDialog>[0]> = {}) {
  const onApply = vi.fn();
  const onClose = vi.fn();
  const onRetry = vi.fn();
  render(
    <ComponentRangeSplitDialog
      preview={null}
      loading={false}
      busy={false}
      error={null}
      onClose={onClose}
      onRetry={onRetry}
      onApply={onApply}
      {...overrides}
    />
  );
  return { onApply, onClose, onRetry };
}

describe("ComponentRangeSplitDialog", () => {
  it("renders the backend rows with their own reasons", () => {
    renderDialog({ preview: plan() });

    expect(screen.getByText("1-1#板~1-25#板")).toBeInTheDocument();
    expect(screen.getByText("将展开绑定")).toBeInTheDocument();
    // 行级原因码由后端给：不是范围 ≠ 台账里没有。
    expect(screen.getByText("该编号不是可展开的构件范围")).toBeInTheDocument();
  });

  // 展开只是给同一条来源病害多挂实例，总量口径因此是实例数，不是"病害被复制了几条"。
  it("summarises the instance change rather than a defect copy count", () => {
    renderDialog({ preview: plan() });
    expect(screen.getByText(/解析实例 4 → 28/)).toBeInTheDocument();
    expect(screen.getByText(/重算评分树 24 条/)).toBeInTheDocument();
  });

  it("applies by plan token", async () => {
    const { onApply } = renderDialog({ preview: plan() });
    await userEvent.click(screen.getByRole("button", { name: "确认拆分" }));
    expect(onApply).toHaveBeenCalledWith("22222222-2222-4222-8222-222222222222");
  });

  it("disables applying when the plan would expand nothing", () => {
    renderDialog({ preview: plan({ will_apply_count: 0 }) });
    expect(screen.getByRole("button", { name: "确认拆分" })).toBeDisabled();
  });

  it("offers a retry instead of an apply when the preview failed", () => {
    renderDialog({ error: "台账版本已变化，请重新计算。" });
    expect(screen.getByRole("alert")).toHaveTextContent(/台账版本已变化/);
    expect(screen.getByRole("button", { name: "重新计算" })).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "确认拆分" })).not.toBeInTheDocument();
  });
});
