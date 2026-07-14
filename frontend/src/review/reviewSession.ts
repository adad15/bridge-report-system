import type { ContractCompatibility } from "../api/reviewApi";

export interface ReviewSession {
  readOnly: boolean;
  bannerText: string | null;
}

export function isImportRecordEditable(importStatus: string): boolean {
  return importStatus === "待校对";
}

export function shouldClearDirtyAfterSave(saveRevision: number, currentRevision: number): boolean {
  return saveRevision === currentRevision;
}

export function deriveReviewSession(
  importStatus: string,
  contractCompatibility: ContractCompatibility
): ReviewSession {
  if (importStatus === "待校对" && contractCompatibility === "native_1_2") {
    return { readOnly: false, bannerText: null };
  }
  if (importStatus === "待校对" && contractCompatibility === "legacy_pending_reparse") {
    return {
      readOnly: true,
      bannerText: "该草稿为旧版合同（1.0/1.1），请重新解析为 1.2 后再校对。",
    };
  }
  if (importStatus === "已确认") {
    return { readOnly: true, bannerText: "本导入记录已确认入库，页面转为只读。" };
  }
  if (importStatus === "已取消") {
    return { readOnly: true, bannerText: "本导入记录已取消，页面转为只读。" };
  }
  if (contractCompatibility === "legacy_read_only") {
    return { readOnly: true, bannerText: "本导入记录为旧版终态数据，页面转为只读。" };
  }
  return { readOnly: true, bannerText: `本导入记录状态为“${importStatus}”，页面转为只读。` };
}
