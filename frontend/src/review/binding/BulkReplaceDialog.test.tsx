import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import type { BindingReplaceInventoryEntry, BindingRow } from "../../api/importBindingApi";
import { BulkReplaceDialog } from "./BulkReplaceDialog";

function row(component_number: string, status: BindingRow["status"] = "unmatched"): BindingRow {
  return {
    component_number,
    defect_count: 1,
    status,
    bridge_component_id: null,
    bound_component: null,
    candidate_components: [],
  };
}

function entry(id: string, component_number: string): BindingReplaceInventoryEntry {
  return { bridge_component_id: id, component_number, is_active: true };
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
  // 取数完成前用空数组顶替，会让预览把每一条都判成"台账中无此编号"——不报错、
  // 不崩溃，只是全错。所以未加载必须是 null，而且此时不许输入。
  it("does not preview before the inventory arrives", async () => {
    renderDialog({ entries: null, loading: true });

    expect(screen.getByText("正在加载台账构件…")).toBeInTheDocument();
    expect(screen.getByLabelText("查找")).toBeDisabled();
    expect(screen.getByLabelText("替换为")).toBeDisabled();
    expect(screen.queryByText("台账中无此编号")).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "应用" })).toBeDisabled();
  });

  it("offers a retry when the inventory failed to load", async () => {
    const onRetry = vi.fn();
    renderDialog({ entries: null, loading: false, onRetry });

    await userEvent.click(screen.getByRole("button", { name: "重试" }));
    expect(onRetry).toHaveBeenCalled();
  });


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
