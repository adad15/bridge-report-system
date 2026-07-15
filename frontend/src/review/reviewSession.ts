import type { ContractCompatibility, ReopenScope } from "../api/reviewApi";

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
  contractCompatibility: ContractCompatibility,
  reopenScope: ReopenScope | null = null
): ReviewSession {
  if (importStatus === "待校对" && contractCompatibility === "native_1_2" && reopenScope !== null) {
    // 重开校对态：可编辑，但顶部横幅提示范围与后续流程（修订版入库）。
    return {
      readOnly: false,
      bannerText:
        reopenScope === "full"
          ? "已重开校对（全部病害可修改）。修改完成后需保存草稿、通过入库前检查并确认修订版入库。"
          : "已重开校对（仅带警告的病害可修改）。修改完成后需保存草稿、通过入库前检查并确认修订版入库。",
    };
  }
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
