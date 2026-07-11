import { describe, expect, it } from "vitest";

import { deriveReviewSession, isImportRecordEditable, shouldClearDirtyAfterSave } from "./reviewSession";

describe("review session guards", () => {
  it("only allows pending import records to be edited", () => {
    expect(isImportRecordEditable("待校对")).toBe(true);
    expect(isImportRecordEditable("已确认")).toBe(false);
    expect(isImportRecordEditable("已取消")).toBe(false);
  });

  it("only clears dirty when no newer edit happened during save", () => {
    expect(shouldClearDirtyAfterSave(11, 11)).toBe(true);
    expect(shouldClearDirtyAfterSave(11, 12)).toBe(false);
  });
});

describe("deriveReviewSession", () => {
  it("keeps a pending upgraded 1.0 review editable", () => {
    expect(deriveReviewSession("待校对", "upgraded_1_0")).toEqual({
      readOnly: false,
      bannerText: null,
    });
  });

  it("makes a confirmed review read-only with a confirmed banner", () => {
    expect(deriveReviewSession("已确认", "native_1_1")).toEqual({
      readOnly: true,
      bannerText: "本导入记录已确认入库，页面转为只读。",
    });
  });

  it("makes a cancelled review read-only with a cancelled banner", () => {
    expect(deriveReviewSession("已取消", "native_1_1")).toEqual({
      readOnly: true,
      bannerText: "本导入记录已取消，页面转为只读。",
    });
  });

  it("makes legacy terminal data read-only even when its status says pending", () => {
    expect(deriveReviewSession("待校对", "legacy_read_only")).toEqual({
      readOnly: true,
      bannerText: "本导入记录为旧版终态数据，页面转为只读。",
    });
  });
});
