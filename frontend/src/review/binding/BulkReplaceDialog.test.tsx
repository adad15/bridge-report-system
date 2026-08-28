import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { BulkReplaceDialog } from "./BulkReplaceDialog";
import type { ResolutionPlanPreview, ResolutionPlanRow } from "../../api/resolutionApi";

// 5.0：预览由后端生成，前端只展示并拿 plan token 去执行。这些用例因此不再验证
// "前端算得对不对"（那份规则已经迁到 C++，由 ComponentReplacePatternTest 守），
// 而是验证"后端给什么就显示什么、应用时提交的是 token"。

function planRow(overrides: Partial<ResolutionPlanRow> = {}): ResolutionPlanRow {
  return {
    group_id: `group-${overrides.source_component_number ?? "x"}`,
    source_component_name: "桥面铺装",
    source_component_number: "第32孔桥面",
    member_count: 1,
    resolved_numbers: ["32#跨桥面铺装"],
    target_component_ids: ["c32"],
    outcome: "will_bind",
    reason_code: "",
    reason_message: "",
    ...overrides,
  };
}

function plan(overrides: Partial<ResolutionPlanPreview> = {}): ResolutionPlanPreview {
  return {
    plan_token: "11111111-1111-4111-8111-111111111111",
    operation_type: "bulk_replace",
    expires_at: "2026-08-27T10:15:00+08:00",
    will_apply_count: 2,
    skipped_count: 1,
    blocked_count: 0,
    instances_before: 3,
    instances_after: 3,
    rating_recomputed_count: 2,
    inventory_revision_id: "revision-1",
    rating_tree_version_id: "tree-1",
    rows: [
      planRow(),
      planRow({
        source_component_number: "第33孔桥面",
        resolved_numbers: ["33#跨桥面铺装"],
        target_component_ids: ["c33"],
      }),
      planRow({
        source_component_number: "第7孔桥面",
        resolved_numbers: ["7#跨桥面铺装"],
        target_component_ids: [],
        outcome: "skipped",
        reason_code: "component_not_found",
        reason_message: "台账中无此编号。",
      }),
    ],
    ...overrides,
  };
}

function renderDialog(overrides: Partial<Parameters<typeof BulkReplaceDialog>[0]> = {}) {
  const onPreview = vi.fn().mockResolvedValue(undefined);
  const onApply = vi.fn().mockResolvedValue(undefined);
  const onClose = vi.fn();
  render(
    <BulkReplaceDialog
      partName="桥面铺装"
      plan={null}
      previewing={false}
      busy={false}
      onPreview={onPreview}
      onApply={onApply}
      onClose={onClose}
      {...overrides}
    />
  );
  return { onPreview, onApply, onClose };
}

describe("BulkReplaceDialog", () => {
  it("asks the backend for a preview instead of computing one", async () => {
    const { onPreview } = renderDialog();
    await userEvent.type(screen.getByLabelText("查找"), "第*孔桥面");
    await userEvent.type(screen.getByLabelText("替换为"), "*#跨桥面铺装");
    await userEvent.click(screen.getByRole("button", { name: "生成预览" }));

    expect(onPreview).toHaveBeenCalledWith("第*孔桥面", "*#跨桥面铺装");
  });

  it("renders the backend rows with their own reasons and totals", () => {
    renderDialog({ plan: plan() });

    expect(screen.getByText("32#跨桥面铺装")).toBeInTheDocument();
    expect(screen.getByText("33#跨桥面铺装")).toBeInTheDocument();
    // 行级原因码由后端给；"台账中无此编号"与"不符合模式"必须能分开看。
    expect(screen.getByText("台账中无此编号")).toBeInTheDocument();
    expect(screen.getByText(/将绑定 2 行/)).toBeInTheDocument();
    expect(screen.getByText(/跳过 1 行/)).toBeInTheDocument();
  });

  it("disables previewing until a pattern is entered", () => {
    renderDialog();
    expect(screen.getByRole("button", { name: "生成预览" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "应用" })).toBeDisabled();
  });

  it("disables applying when the plan would bind nothing", () => {
    renderDialog({ plan: plan({ will_apply_count: 0 }) });
    expect(screen.getByRole("button", { name: "应用" })).toBeDisabled();
  });

  it("applies by plan token, not by a locally computed target list", async () => {
    const { onApply } = renderDialog({ plan: plan() });
    await userEvent.click(screen.getByRole("button", { name: "应用" }));

    expect(onApply).toHaveBeenCalledWith("11111111-1111-4111-8111-111111111111");
  });

  it("surfaces a backend error without pretending the plan is usable", () => {
    renderDialog({ error: "替换内容里的 * 比查找内容多（2 > 1），多出的无从取值。" });
    expect(screen.getByRole("alert")).toHaveTextContent(/比查找内容多/);
    expect(screen.getByRole("button", { name: "应用" })).toBeDisabled();
  });
});
