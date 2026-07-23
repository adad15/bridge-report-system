import type { WorkspaceImport, WorkspaceInspection } from "../api/workspaceApi";

export type InspectionProgressStage =
  | "empty"
  | "uploaded"
  | "parsing"
  | "parse_failed"
  | "review"
  | "completed";

export interface InspectionProgress {
  stage: InspectionProgressStage;
  label: string;
}

export function deriveInspectionProgress(
  inspection: Pick<WorkspaceInspection, "status">,
  imports: Array<Pick<WorkspaceImport, "import_status">>
): InspectionProgress {
  if (inspection.status === "已确认" || inspection.status === "已归档") {
    return { stage: "completed", label: inspection.status };
  }
  const statuses = new Set(imports.map((item) => item.import_status));
  if (statuses.has("待校对")) return { stage: "review", label: "待校对" };
  if (statuses.has("解析中")) return { stage: "parsing", label: "解析中" };
  if (statuses.has("解析失败")) return { stage: "parse_failed", label: "解析失败" };
  if (statuses.has("已上传")) return { stage: "uploaded", label: "待解析" };
  if (statuses.has("已确认")) return { stage: "completed", label: "已确认" };
  return { stage: "empty", label: "待导入资料" };
}

const segment = (value: string) => encodeURIComponent(value);

export const bridgeOverviewPath = (bridgeId: string) => `/bridges/${segment(bridgeId)}`;
export const inspectionsPath = (bridgeId: string) => `${bridgeOverviewPath(bridgeId)}/inspections`;
export const inspectionWorkspacePath = (bridgeId: string, inspectionYearId: string) =>
  `${inspectionsPath(bridgeId)}/${segment(inspectionYearId)}`;
export const componentArchivePath = (bridgeId: string) => `${bridgeOverviewPath(bridgeId)}/components`;
// 台账单列一页：构件可达数千条，挂在总览页会让每次进桥都先等它加载完。
export const componentInventoryPath = (bridgeId: string) =>
  `${bridgeOverviewPath(bridgeId)}/inventory`;
export const reviewPath = (bridgeId: string, inspectionYearId: string, importRecordId: string) =>
  `${inspectionWorkspacePath(bridgeId, inspectionYearId)}/imports/${segment(importRecordId)}/review`;
export const componentBindingPath = (bridgeId: string, inspectionYearId: string, importRecordId: string) =>
  `${inspectionWorkspacePath(bridgeId, inspectionYearId)}/imports/${segment(importRecordId)}/binding`;
