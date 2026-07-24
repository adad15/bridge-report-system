import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import type { ComponentInventoryEntry } from "../../api/componentInventoryApi";
import type { BindingRow } from "../../api/importBindingApi";
import { BulkReplaceDialog } from "./BulkReplaceDialog";

function row(component_number: string, status: BindingRow["status"] = "unmatched"): BindingRow {
  return {
    component_number,
    defect_count: 1,
    status,
    bridge_component_id: null,
    candidate_component_ids: [],
  };
}

function entry(id: string, component_number: string): ComponentInventoryEntry {
  return {
    id: `entry-${id}`,
    bridge_component_id: id,
    component_number,
    site_name: "桥面铺装",
    site_component_type: "桥面铺装",
    span_or_location: null,
    is_active: true,
    deactivated_at: null,
    deactivation_reason: null,
    sort_order: 1,
    remarks: null,
    is_referenced: false,
    mappings: [],
  };
}

const rows = [row("第32孔桥面"), row("第33孔桥面"), row("第7孔桥面")];
const entries = [entry("c32", "32#跨桥面铺装"), entry("c33", "33#跨桥面铺装")];

function renderDialog(overrides: Partial<Parameters<typeof BulkReplaceDialog>[0]> = {}) {
  const onApply = vi.fn().mockResolvedValue(undefined);
  const onClose = vi.fn();
  render(
    <BulkReplaceDialog
      partName="桥面铺装"
      rows={rows}
      entries={entries}
      busy={false}
      onApply={onApply}
      onClose={onClose}
      {...overrides}
    />
  );
  return { onApply, onClose };
}

async function fillPattern(find: string, replace: string) {
  await userEvent.type(screen.getByLabelText("查找"), find);
  await userEvent.type(screen.getByLabelText("替换为"), replace);
}

describe("BulkReplaceDialog", () => {
  it("previews each row and totals bindable versus skipped", async () => {
    renderDialog();
    await fillPattern("第*孔桥面", "*#跨桥面铺装");

    expect(await screen.findByText("32#跨桥面铺装")).toBeInTheDocument();
    expect(screen.getByText("33#跨桥面铺装")).toBeInTheDocument();
    // 第7孔转换成功但台账里没有，应与"不符合模式"区分开。
    expect(screen.getByText("台账中无此编号")).toBeInTheDocument();
    expect(screen.getByText(/将绑定 2 行/)).toBeInTheDocument();
    expect(screen.getByText(/跳过 1 行/)).toBeInTheDocument();
  });

  it("disables applying while no pattern is entered", () => {
    renderDialog();
    expect(screen.getByRole("button", { name: "应用" })).toBeDisabled();
  });

  it("reports an invalid pattern and keeps applying disabled", async () => {
    renderDialog();
    await fillPattern("第*孔桥面", "*-*#板");
    expect(await screen.findByText(/替换内容里的 \* 比查找内容多/)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "应用" })).toBeDisabled();
  });

  it("disables applying when nothing would bind", async () => {
    renderDialog();
    await fillPattern("第*孔铰缝", "*#铰缝");
    await waitFor(() => expect(screen.getByText(/将绑定 0 行/)).toBeInTheDocument());
    expect(screen.getByRole("button", { name: "应用" })).toBeDisabled();
  });

  it("applies only the rows that resolve to exactly one component", async () => {
    const { onApply } = renderDialog();
    await fillPattern("第*孔桥面", "*#跨桥面铺装");
    await userEvent.click(await screen.findByRole("button", { name: "应用" }));

    expect(onApply).toHaveBeenCalledWith([
      { part_name: "桥面铺装", component_number: "第32孔桥面", bridge_component_id: "c32" },
      { part_name: "桥面铺装", component_number: "第33孔桥面", bridge_component_id: "c33" },
    ]);
  });
});
