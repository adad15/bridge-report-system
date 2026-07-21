import { describe, expect, it } from "vitest";

import { expandTemplate } from "./inventoryNumbering";

describe("expandTemplate", () => {
  it("expands span×member like the backend", () => {
    const out = expandTemplate("{span}-{c1}#{name}", "梁", [13], 5);
    expect(out.length).toBe(65);
    expect(out[0]).toEqual({ number: "1-1#梁", location: "第1孔" });
    expect(out.at(-1)).toEqual({ number: "5-13#梁", location: "第5孔" });
  });

  it("expands three-level diaphragm", () => {
    const out = expandTemplate("{span}-{c1}-{c2}#{name}", "横隔梁", [12, 2], 5);
    expect(out.length).toBe(120);
    expect(out[0].number).toBe("1-1-1#横隔梁");
    expect(out.at(-1)!.number).toBe("5-12-2#横隔梁");
  });

  it("expands support line, abutment×side", () => {
    expect(expandTemplate("{line}{name}", "基础", [], 5).map((x) => x.number)).toEqual([
      "0#台基础", "1#墩基础", "2#墩基础", "3#墩基础", "4#墩基础", "5#台基础",
    ]);
    expect(expandTemplate("{ab}#台{side}侧{name}", "翼墙", [], 5).map((x) => x.number)).toEqual([
      "0#台左侧翼墙", "0#台右侧翼墙", "5#台左侧翼墙", "5#台右侧翼墙",
    ]);
    expect(expandTemplate("{side}侧{name}", "主缆", [], 4).map((x) => x.number)).toEqual([
      "左侧主缆", "右侧主缆",
    ]);
  });

  it("handles whole-bridge, sequential count, and pier line", () => {
    expect(expandTemplate("{name}", "排水系统", [], 5)).toEqual([{ number: "排水系统", location: "" }]);
    expect(expandTemplate("{c1}#{name}", "索塔", [2], 5).map((x) => x.number)).toEqual([
      "1#索塔", "2#索塔",
    ]);
    expect(expandTemplate("{pier}#墩{name}", "盖梁", [], 3).map((x) => x.number)).toEqual([
      "1#墩盖梁", "2#墩盖梁",
    ]);
  });
});
