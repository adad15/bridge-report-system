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
    expect(deriveReviewSession("待校对", "native_5_0")).toEqual({
      readOnly: false,
      bannerText: null,
    });
  });

  it("makes a confirmed review read-only with a confirmed banner", () => {
    expect(deriveReviewSession("已确认", "native_5_0")).toEqual({
      readOnly: true,
      bannerText: "本导入记录已确认入库，页面转为只读。",
    });
  });

  it("makes a cancelled review read-only with a cancelled banner", () => {
    expect(deriveReviewSession("已取消", "native_5_0")).toEqual({
      readOnly: true,
      bannerText: "本导入记录已取消，页面转为只读。",
    });
  });

  it("keeps a reopened warnings_only review editable with a scoped banner", () => {
    const session = deriveReviewSession("待校对", "native_5_0", "warnings_only");
    expect(session.readOnly).toBe(false);
    expect(session.bannerText).toContain("仅带警告的病害可修改");
    expect(session.bannerText).toContain("修订版入库");
  });

  it("keeps a reopened full review editable with a full-scope banner", () => {
    const session = deriveReviewSession("待校对", "native_5_0", "full");
    expect(session.readOnly).toBe(false);
    expect(session.bannerText).toContain("全部病害可修改");
  });

});

// 一份正常的 5.0 待校对记录必须可编辑。
//
// 之前这里的两个"可编辑"分支写死了 native_4_0，而后端在契约 5.0 改造时已经改回
// native_5_0。两边一分叉，待校对记录就落进最后那个兜底分支——它本意是接住"解析中"
// 这类真正不可编辑的状态，于是页面整个变成只读，还打出一句自相矛盾的
// "本导入记录状态为『待校对』，页面转为只读"。
//
// 当时前端测试全绿：夹具里写的也是 native_4_0，实现和测试一起过时，互相印证着对方。
// 所以这条用例直接钉住后端真正会发的那个值。
it("keeps a plain 5.0 pending record editable", () => {
  const session = deriveReviewSession("待校对", "native_5_0");
  expect(session.readOnly).toBe(false);
  expect(session.bannerText).toBeNull();
});

it("keeps a reopened 5.0 record editable and explains the scope", () => {
  const session = deriveReviewSession("待校对", "native_5_0", "warnings_only");
  expect(session.readOnly).toBe(false);
  expect(session.bannerText).toContain("仅带警告的病害可修改");
});

// 兜底分支只该接住真正不可编辑的状态。
it("falls back to read-only only for statuses that really are", () => {
  const session = deriveReviewSession("解析中", "native_5_0");
  expect(session.readOnly).toBe(true);
  expect(session.bannerText).toContain("解析中");
});
