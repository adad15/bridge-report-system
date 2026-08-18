import { describe, expect, it } from "vitest";

import type { BindingReplaceInventoryEntry, BindingRow } from "../../api/importBindingApi";
import { normalizeComponentNumber } from "./normalizeComponentNumber";
import { buildReplacePreview } from "./replacePreview";

function row(component_number: string, status: BindingRow["status"]): BindingRow {
  return {
    component_number,
    defect_count: 1,
    status,
    bridge_component_id: status === "bound" ? "bound-id" : null,
    bound_component: null,
    candidate_components: [],
  };
}

function entry(id: string, component_number: string): BindingReplaceInventoryEntry {
  return { bridge_component_id: id, component_number, is_active: true };
}

const entries = [entry("c32", "32#跨桥面铺装"), entry("c33", "33#跨桥面铺装")];

describe("normalizeComponentNumber", () => {
  // 镜像 C++ normalize_component_number；这三类输入是两边最容易分叉的地方。
  it("mirrors the C++ normalisation for full-width and spacing variants", () => {
    expect(normalizeComponentNumber("32＃跨桥面铺装")).toBe("32#跨桥面铺装");
    expect(normalizeComponentNumber("　32# 跨桥面铺装 ")).toBe("32#跨桥面铺装");
    expect(normalizeComponentNumber("32#ABC")).toBe("32#abc");
    expect(normalizeComponentNumber("1－1#板")).toBe("1-1#板");
    expect(normalizeComponentNumber("1-1##")).toBe("1-1");
  });
});

describe("buildReplacePreview", () => {
  it("marks a row that resolves to exactly one inventory component", () => {
    const preview = buildReplacePreview(
      [row("第32孔桥面", "unmatched")], entries, "第*孔桥面", "*#跨桥面铺装");
    expect(preview.ok).toBe(true);
    if (!preview.ok) return;
    expect(preview.items).toEqual([
      expect.objectContaining({
        componentNumber: "第32孔桥面",
        outcome: "will_bind",
        targetNumber: "32#跨桥面铺装",
        bridgeComponentId: "c32",
      }),
    ]);
    expect(preview.bindableCount).toBe(1);
    expect(preview.skippedCount).toBe(0);
  });

  it("separates a pattern miss from a lookup miss", () => {
    const preview = buildReplacePreview(
      [row("3-5#铰缝", "unmatched"), row("第7孔桥面", "unmatched")],
      entries, "第*孔桥面", "*#跨桥面铺装");
    if (!preview.ok) throw new Error("模式应当合法");
    // 不符合模式 与 台账里没有，是两回事：前者说明模式写错，后者说明台账确实没有。
    expect(preview.items[0].outcome).toBe("pattern_miss");
    expect(preview.items[1].outcome).toBe("not_in_inventory");
    expect(preview.bindableCount).toBe(0);
    expect(preview.skippedCount).toBe(2);
  });

  it("skips a transformed number that hits more than one component", () => {
    const duplicated = [...entries, entry("c32b", "32#跨桥面铺装")];
    const preview = buildReplacePreview(
      [row("第32孔桥面", "unmatched")], duplicated, "第*孔桥面", "*#跨桥面铺装");
    if (!preview.ok) throw new Error("模式应当合法");
    expect(preview.items[0].outcome).toBe("ambiguous");
    expect(preview.bindableCount).toBe(0);
  });

  it("leaves resolved rows out entirely", () => {
    const preview = buildReplacePreview(
      [row("第32孔桥面", "bound"), row("第33孔桥面", "missing"), row("第32孔桥面", "ambiguous")],
      entries, "第*孔桥面", "*#跨桥面铺装");
    if (!preview.ok) throw new Error("模式应当合法");
    // 已绑定与已标记缺失不参与；歧义行参与。
    expect(preview.items).toHaveLength(1);
    expect(preview.items[0].outcome).toBe("will_bind");
  });

  it("matches through normalisation differences", () => {
    const oddEntries = [entry("c32", "32＃跨桥面铺装 ")];
    const preview = buildReplacePreview(
      [row("第32孔桥面", "unmatched")], oddEntries, "第*孔桥面", "*#跨桥面铺装");
    if (!preview.ok) throw new Error("模式应当合法");
    expect(preview.items[0].outcome).toBe("will_bind");
    expect(preview.items[0].bridgeComponentId).toBe("c32");
  });

  it("reports an invalid pattern instead of a preview", () => {
    const preview = buildReplacePreview(
      [row("第32孔桥面", "unmatched")], entries, "第*孔桥面", "*-*#板");
    expect(preview.ok).toBe(false);
    if (!preview.ok) expect(preview.error).toMatch(/替换/);
  });

  it("ignores deactivated inventory entries", () => {
    const inactive = [{ ...entry("c32", "32#跨桥面铺装"), is_active: false }];
    const preview = buildReplacePreview(
      [row("第32孔桥面", "unmatched")], inactive, "第*孔桥面", "*#跨桥面铺装");
    if (!preview.ok) throw new Error("模式应当合法");
    expect(preview.items[0].outcome).toBe("not_in_inventory");
  });
});
