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
  it("keeps a pending native 2.0 review editable", () => {
    expect(deriveReviewSession("待校对", "native_2_0")).toEqual({
      readOnly: false,
      bannerText: null,
    });
  });

  it("makes a confirmed review read-only with a confirmed banner", () => {
    expect(deriveReviewSession("已确认", "native_2_0")).toEqual({
      readOnly: true,
      bannerText: "本导入记录已确认入库，页面转为只读。",
    });
  });

  it("makes a cancelled review read-only with a cancelled banner", () => {
    expect(deriveReviewSession("已取消", "native_2_0")).toEqual({
      readOnly: true,
      bannerText: "本导入记录已取消，页面转为只读。",
    });
  });

  it("keeps a reopened warnings_only review editable with a scoped banner", () => {
    const session = deriveReviewSession("待校对", "native_2_0", "warnings_only");
    expect(session.readOnly).toBe(false);
    expect(session.bannerText).toContain("仅带警告的病害可修改");
    expect(session.bannerText).toContain("修订版入库");
  });

  it("keeps a reopened full review editable with a full-scope banner", () => {
    const session = deriveReviewSession("待校对", "native_2_0", "full");
    expect(session.readOnly).toBe(false);
    expect(session.bannerText).toContain("全部病害可修改");
  });

});
