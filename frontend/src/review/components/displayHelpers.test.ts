import { describe, expect, it } from "vitest";

import type { AttentionItem } from "../grouping";
import { formatAttentionItem } from "./displayHelpers";

describe("formatAttentionItem", () => {
  it.each<AttentionItem>([
    { kind: "defect", candidateId: "defect_0001", message: "构件名称疑似缺失。", severity: "warning" },
    { kind: "photo", candidateId: "photo_0002", message: "未关联病害。", severity: "error" },
    { kind: "rating", candidateId: "assessment", message: "系统评定缺少输入。", severity: "warning" },
    { kind: "import", candidateId: "unknown_9999", message: "无法定位到具体候选。", severity: "info" },
  ])("formats $kind attention items", (item) => {
    expect(formatAttentionItem(item)).toBe(`[${item.kind}] ${item.candidateId}: ${item.message}`);
  });
});
