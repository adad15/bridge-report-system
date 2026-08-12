import { describe, expect, it } from "vitest";

import { displayDefectLocation, displayMatchEvidence } from "./displayHelpers";

describe("displayDefectLocation", () => {
  it("hides the Word placeholders that stand for an empty location", () => {
    for (const placeholder of ["", "  ", "/", "／", null, undefined]) {
      expect(displayDefectLocation(placeholder)).toBeNull();
    }
  });

  it("keeps a real location and trims it", () => {
    expect(displayDefectLocation(" 左侧翼缘板及腹板 ")).toBe("左侧翼缘板及腹板");
    expect(displayDefectLocation("1/4 跨")).toBe("1/4 跨");
  });
});

describe("displayMatchEvidence", () => {
  it("says nothing for matches the node name already explains", () => {
    expect(displayMatchEvidence("病害类型“锈蚀”与规范病害名称完全一致。", "exact")).toBe("");
    expect(displayMatchEvidence("病害类型“渗水泛碱”命中受控别名“渗水泛碱”。", "controlled_alias")).toBe("");
  });

  it("keeps the layers that point at a fragment of the description", () => {
    expect(displayMatchEvidence("命中受控关键词“剥蚀”。", "controlled_keyword"))
      .toBe("命中受控关键词“剥蚀”。");
    expect(displayMatchEvidence("叙述片段命中受控别名“水损”。", "fuzzy_candidate"))
      .toBe("叙述片段命中受控别名“水损”。");
  });

  // 存量草稿存的是旧文字，而已确认的病害不会再重新匹配，只能在展示层滤掉。
  it("strips boilerplate left in drafts written before the rules changed", () => {
    expect(displayMatchEvidence(
      "命中受控关键词“剥蚀”（规则 org.bridge.keyword.spalling.upper_bearing）。 "
        + "构件适用依据：桥型 h21.bridge_type.beam 与规范构件类别 h21.component.beam.upper_general 均在该节点的适用范围内。",
      "controlled_keyword",
    )).toBe("命中受控关键词“剥蚀”。");
  });

  it("survives a missing evidence without printing undefined", () => {
    expect(displayMatchEvidence(null, "controlled_keyword")).toBe("");
    expect(displayMatchEvidence(undefined, undefined)).toBe("");
  });
});
