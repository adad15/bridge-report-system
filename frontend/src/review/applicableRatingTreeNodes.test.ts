import { describe, expect, it } from "vitest";

import type { RatingTreeNodeSummary } from "../api/ratingTreeApi";
import { applicableRatingTreeNodes } from "./applicableRatingTreeNodes";

// 区间展开的病害一条挂多件构件，`bridgeComponentId` 按约定为 null。下拉框此前按那个
// 单一 id 取适用节点，于是展开过的病害一个选项都没有——界面上表现为"评定树选不了"，
// 而后端解析表里其实只是"没有精确规则、等人工挑"。

function node(id: string, order = 0): RatingTreeNodeSummary {
  return {
    id,
    node_key: id,
    parent_node_id: null,
    display_number: null,
    display_name: id,
    node_type: "defect",
    sort_order: order,
    bridge_type_ids: ["h21.bridge.girder"],
    component_category_ids: ["h21.component.deck.slab"],
    scoring_mode: "inherit_h21",
    h21_indicator_id: null,
    is_selectable: true,
    is_scoring: true,
  } as RatingTreeNodeSummary;
}

describe("applicableRatingTreeNodes", () => {
  it("returns the single component's nodes", () => {
    const table = new Map([["c-1", [node("n1"), node("n2")]]]);
    expect(applicableRatingTreeNodes(["c-1"], table).map((item) => item.id))
      .toEqual(["n1", "n2"]);
  });

  // 展开成多件同类别构件是最常见的情形：三件的适用集合一样，交集就是它本身。
  it("keeps every node shared by all components of a range-expanded defect", () => {
    const nodes = [node("n1"), node("n2")];
    const table = new Map([["c-1", nodes], ["c-2", nodes], ["c-3", nodes]]);
    expect(applicableRatingTreeNodes(["c-1", "c-2", "c-3"], table).map((item) => item.id))
      .toEqual(["n1", "n2"]);
  });

  // 交集而不是并集：只对其中一件成立的节点选了也会被判据打回。
  it("drops nodes that do not apply to every component", () => {
    const table = new Map([
      ["c-1", [node("shared"), node("only-first")]],
      ["c-2", [node("shared")]],
    ]);
    expect(applicableRatingTreeNodes(["c-1", "c-2"], table).map((item) => item.id))
      .toEqual(["shared"]);
  });

  it("returns nothing when no component is bound", () => {
    expect(applicableRatingTreeNodes([], new Map([["c-1", [node("n1")]]]))).toEqual([]);
  });

  // 适用节点表还没加载到这件构件时按空处理，不能拿另一件的集合顶替。
  it("returns nothing when a component is missing from the table", () => {
    const table = new Map([["c-1", [node("n1")]]]);
    expect(applicableRatingTreeNodes(["c-1", "c-2"], table)).toEqual([]);
  });

  it("tolerates repeated component ids", () => {
    const table = new Map([["c-1", [node("n1")]]]);
    expect(applicableRatingTreeNodes(["c-1", "c-1"], table).map((item) => item.id))
      .toEqual(["n1"]);
  });
});
