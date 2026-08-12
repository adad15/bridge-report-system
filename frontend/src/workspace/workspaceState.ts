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

// 状态徽章原本全站共用一个灰底，「在用」和「解析失败」长得一模一样，等于没做徽章。
// 按含义分档：绿=已就位，琥珀=还等人动手，红=出错；已归档、停用这类终态留灰。
const kOkStatuses = new Set(["在用", "已确认"]);
const kWarnStatuses = new Set(["待校对", "待确认", "草稿", "已上传", "待解析", "待导入资料", "解析中"]);
const kDangerStatuses = new Set(["解析失败"]);

export type StatusTone = "ok" | "warn" | "danger" | "neutral";

export function statusTone(status: string): StatusTone {
  if (kOkStatuses.has(status)) return "ok";
  if (kWarnStatuses.has(status)) return "warn";
  if (kDangerStatuses.has(status)) return "danger";
  return "neutral";
}

export const statusBadgeClass = (status: string) => `status-badge status-badge-${statusTone(status)}`;

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
