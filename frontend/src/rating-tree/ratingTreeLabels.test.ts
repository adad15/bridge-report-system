import { describe, expect, it } from "vitest";

import { ratingTreeDisplayLabel } from "./ratingTreeLabels";

describe("ratingTreeDisplayLabel", () => {
  it("formats organization group numbers like the reference tree", () => {
    expect(ratingTreeDisplayLabel({
      node_key: "org.bridge.group.5_1_1",
      node_type: "component_group",
      display_name: "上部承重构件、上部一般构件",
    })).toBe("5.1.1、上部承重构件、上部一般构件");
  });

  it("formats defect numbers with the leaf sequence after a dash", () => {
    expect(ratingTreeDisplayLabel({
      node_key: "org.bridge.defect.5_1_1_3",
      node_type: "defect",
      display_name: "空洞、孔洞",
    })).toBe("5.1.1-3、空洞、孔洞");
  });

  it("uses the referenced H21 number for shared defect nodes", () => {
    expect(ratingTreeDisplayLabel({
      node_key: "org.bridge.defect.6_1_3__5_1_1_1",
      node_type: "defect",
      display_name: "蜂窝、麻面",
    })).toBe("5.1.1-1、蜂窝、麻面");
  });
});
