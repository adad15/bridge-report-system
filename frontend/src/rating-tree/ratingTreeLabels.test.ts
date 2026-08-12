import { describe, expect, it } from "vitest";

import type { RatingTreeNodeSummary } from "../api/ratingTreeApi";
import {
  ratingTreeDisplayLabel,
  ratingTreeOptionLabel,
  sortRatingTreeNodes,
} from "./ratingTreeLabels";

function optionNode(
  id: string,
  displayNumber: string,
  displayName: string,
  parentNumber = displayNumber.split("-")[0],
  parentName = "上部承重构件、上部一般构件",
): RatingTreeNodeSummary {
  const parentId = `group-${parentNumber}`;
  const displayNumberParts = displayNumber.split("-");
  return {
    id,
    node_key: `org.bridge.defect.${displayNumber.replace(/[.-]/g, "_")}`,
    parent_node_id: parentId,
    display_number: displayNumber,
    display_name: displayName,
    node_type: "defect",
    sort_order: Number(displayNumberParts[displayNumberParts.length - 1]) * 10,
    bridge_type_ids: [],
    component_category_ids: [],
    scoring_mode: "non_scoring",
    h21_indicator_id: null,
    is_selectable: true,
    is_scoring: false,
    path: [{
      id: parentId,
      node_key: `org.bridge.group.${parentNumber.replace(/\./g, "_")}`,
      display_number: parentNumber,
      display_name: parentName,
      node_type: "component_group",
    }],
  };
}

describe("ratingTreeDisplayLabel", () => {
  it("formats organization group numbers like the reference tree", () => {
    expect(ratingTreeDisplayLabel({
      node_key: "org.bridge.group.5_1_1",
      node_type: "component_group",
      display_number: null,
      display_name: "上部承重构件、上部一般构件",
    })).toBe("5.1.1 上部承重构件、上部一般构件");
  });

  it("formats defect numbers with the leaf sequence after a dash", () => {
    expect(ratingTreeDisplayLabel({
      node_key: "org.bridge.defect.5_1_1_3",
      node_type: "defect",
      display_number: null,
      display_name: "空洞、孔洞",
    })).toBe("5.1.1-3 空洞、孔洞");
  });

  it("shows the cap-beam crack as the first defect under section 9.1.2", () => {
    expect(ratingTreeDisplayLabel({
      node_key: "org.bridge.defect.9_1_2",
      node_type: "defect",
      display_number: null,
      display_name: "盖梁裂缝",
    })).toBe("9.1.2-1 盖梁裂缝");
  });

  it("uses the referenced H21 number for shared defect nodes", () => {
    expect(ratingTreeDisplayLabel({
      node_key: "org.bridge.defect.6_1_3__5_1_1_1",
      node_type: "defect",
      display_number: null,
      display_name: "蜂窝、麻面",
    })).toBe("5.1.1-1 蜂窝、麻面");
  });

  it("uses the explicit source number before any legacy key fallback", () => {
    expect(ratingTreeDisplayLabel({
      node_key: "org.bridge.defect.unrelated_key",
      node_type: "defect",
      display_number: "9.1.1-1",
      display_name: "蜂窝、麻面",
    })).toBe("9.1.1-1 蜂窝、麻面");
  });

  it("sorts defects by every numeric part of their displayed number", () => {
    const nodes = [
      optionNode("521-2", "5.2.1-2", "锈蚀"),
      optionNode("511-10", "5.1.1-10", "预应力构件损伤"),
      optionNode("511-2", "5.1.1-2", "剥落、露筋"),
      optionNode("521-1", "5.2.1-1", "涂层劣化"),
    ];

    expect(sortRatingTreeNodes(nodes).map((node) => node.display_number)).toEqual([
      "5.1.1-2",
      "5.1.1-10",
      "5.2.1-1",
      "5.2.1-2",
    ]);
    expect(nodes.map((node) => node.display_number)).toEqual([
      "5.2.1-2",
      "5.1.1-10",
      "5.1.1-2",
      "5.2.1-1",
    ]);
  });

  it("adds the parent group only when another option has the same disease name", () => {
    const concreteOther = optionNode(
      "511-14",
      "5.1.1-14",
      "上部其它病害",
      "5.1.1",
      "上部承重构件、上部一般构件",
    );
    const steelOther = optionNode(
      "521-9",
      "5.2.1-9",
      "上部其它病害",
      "5.2.1",
      "钢结构梁桥上部结构构件",
    );
    const crack = optionNode("511-2", "5.1.1-2", "剥落、露筋");
    const options = [concreteOther, steelOther, crack];

    expect(ratingTreeOptionLabel(concreteOther, options)).toBe(
      "5.1.1-14 上部其它病害（所属：5.1.1 上部承重构件、上部一般构件）",
    );
    expect(ratingTreeOptionLabel(steelOther, options)).toBe(
      "5.2.1-9 上部其它病害（所属：5.2.1 钢结构梁桥上部结构构件）",
    );
    expect(ratingTreeOptionLabel(crack, options)).toBe("5.1.1-2 剥落、露筋");
  });
});
