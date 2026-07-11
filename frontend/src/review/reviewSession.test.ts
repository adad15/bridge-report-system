import { describe, expect, it } from "vitest";

import { deriveReviewSession } from "./reviewSession";

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
