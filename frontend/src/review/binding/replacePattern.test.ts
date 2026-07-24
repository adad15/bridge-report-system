import { describe, expect, it } from "vitest";

import { compilePattern } from "./replacePattern";

function applyOrThrow(find: string, replace: string, text: string): string | null {
  const result = compilePattern(find, replace);
  if (!result.ok) throw new Error(`模式应当合法，却报错：${result.error}`);
  return result.pattern.apply(text);
}

describe("compilePattern", () => {
  it("substitutes a single digit run", () => {
    expect(applyOrThrow("第*孔桥面", "*#跨桥面铺装", "第32孔桥面")).toBe("32#跨桥面铺装");
  });

  it("maps multiple wildcards by position", () => {
    expect(applyOrThrow("第*孔第*片板", "*-*#板", "第3孔第5片板")).toBe("3-5#板");
  });

  it("allows the replacement to use fewer wildcards than the pattern", () => {
    // 替换只用第 1 段数字，第 2 段捕获不使用。
    expect(applyOrThrow("第*孔第*片板", "*#跨", "第3孔第5片板")).toBe("3#跨");
  });

  it("treats every non-wildcard character literally", () => {
    // "." 是正则元字符，但这里必须按字面处理，否则 1x1#板 会被误命中。
    expect(applyOrThrow("1.1#板", "X", "1.1#板")).toBe("X");
    expect(applyOrThrow("1.1#板", "X", "1x1#板")).toBeNull();
  });

  it("requires at least one digit per wildcard", () => {
    expect(applyOrThrow("第*孔桥面", "*#跨", "第孔桥面")).toBeNull();
  });

  it("matches the whole text, not a substring", () => {
    expect(applyOrThrow("第*孔桥面", "*#跨", "上部第32孔桥面附近")).toBeNull();
  });

  it("supports a pattern without wildcards", () => {
    expect(applyOrThrow("第32孔桥面", "32#跨桥面铺装", "第32孔桥面")).toBe("32#跨桥面铺装");
  });

  it("rejects a replacement with more wildcards than the pattern", () => {
    const result = compilePattern("第*孔桥面", "*-*#板");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error).toMatch(/替换/);
  });

  it("rejects an empty pattern", () => {
    expect(compilePattern("", "*#跨").ok).toBe(false);
  });

  it("reports a miss instead of returning an empty string", () => {
    expect(applyOrThrow("第*孔桥面", "*#跨", "3-5#铰缝")).toBeNull();
  });
});
