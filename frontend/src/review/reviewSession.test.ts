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
  it("keeps a pending native 1.2 review editable", () => {
    expect(deriveReviewSession("待校对", "native_1_2")).toEqual({
      readOnly: false,
      bannerText: null,
    });
  });

  it("makes a pending legacy draft read-only and asks for a re-parse", () => {
    expect(deriveReviewSession("待校对", "legacy_pending_reparse")).toEqual({
      readOnly: true,
      bannerText: "该草稿为旧版合同（1.0/1.1），请重新解析为 1.2 后再校对。",
    });
  });

  it("makes a confirmed review read-only with a confirmed banner", () => {
    expect(deriveReviewSession("已确认", "native_1_2")).toEqual({
      readOnly: true,
      bannerText: "本导入记录已确认入库，页面转为只读。",
    });
  });

  it("makes a cancelled review read-only with a cancelled banner", () => {
    expect(deriveReviewSession("已取消", "native_1_2")).toEqual({
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

  it("keeps a reopened warnings_only review editable with a scoped banner", () => {
    const session = deriveReviewSession("待校对", "native_1_2", "warnings_only");
    expect(session.readOnly).toBe(false);
    expect(session.bannerText).toContain("仅带警告的病害可修改");
    expect(session.bannerText).toContain("修订版入库");
  });

  it("keeps a reopened full review editable with a full-scope banner", () => {
    const session = deriveReviewSession("待校对", "native_1_2", "full");
    expect(session.readOnly).toBe(false);
    expect(session.bannerText).toContain("全部病害可修改");
  });

  it("ignores the reopen scope when the stored draft is legacy", () => {
    // 重开端点不会放行旧版合同记录；即便状态异常组合出现，也保持只读。
    expect(deriveReviewSession("待校对", "legacy_pending_reparse", "full").readOnly).toBe(true);
  });
});
