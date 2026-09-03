// 入库前检查的问题文案：把后端的机器码、字段名和候选 id 翻成业务说法。
// 底部操作栏（摘要与明细）和消息面板都要用，所以单独放一处，避免两边各抄一份走散。
import type { PreflightIssue } from "../api/reviewApi";

const PREFLIGHT_CODE_LABELS: Readonly<Record<string, string>> = {
  import_record_wrong_status: "导入记录状态异常",
  contract_validation_failed: "数据格式校验失败",
  import_context_mismatch: "导入上下文不一致",
  candidate_pending_review: "病害尚未确认",
  defect_missing_required_field: "必填信息缺失",
  component_inventory_unconfirmed: "构件台账未确认",
  defect_component_match_required: "构件绑定未完成",
  photo_link_unresolved: "照片关联未完成",
  group_confirmation_required: "病害与照片未联合确认",
  missing_photo_confirmation_required: "引用照片未处理",
  photo_archive_missing: "照片归档缺失",
  defect_location_missing: "详细位置缺失",
  defect_without_photo: "病害缺少照片",
  unreferenced_photo_ignored: "未归属照片",
  measurement_unstructured_kept: "尺寸原文未结构化",
  rating_tree_required: "评定树未锁定",
  rating_tree_unavailable: "评定树不可用",
};

const PREFLIGHT_FIELD_LABELS: Readonly<Record<string, string>> = {
  component_name: "构件名称",
  component_number: "构件编号",
  defect_type: "病害类型",
  defect_location: "详细位置",
  defect_scale: "病害标度",
  defect_description: "病害描述",
};

export function translateIssueMessage(message: string, targetLabels: ReadonlyMap<string, string>): string {
  let translated = message;
  for (const [targetId, label] of targetLabels) {
    if (translated.includes(targetId)) translated = translated.split(targetId).join(label);
  }
  for (const [field, label] of Object.entries(PREFLIGHT_FIELD_LABELS)) {
    if (translated.includes(field)) translated = translated.split(field).join(`“${label}”`);
  }
  return translated;
}

export function formatIssue(issue: PreflightIssue, targetLabels: ReadonlyMap<string, string>): string {
  const title = PREFLIGHT_CODE_LABELS[issue.code] ?? issue.code;
  const message = translateIssueMessage(issue.message, targetLabels);
  const targetLabel = issue.target_candidate_id ? targetLabels.get(issue.target_candidate_id) : undefined;
  const targetSuffix = issue.target_candidate_id && !issue.message.includes(issue.target_candidate_id)
    ? `（${targetLabel ?? issue.target_candidate_id}）`
    : "";
  return `${title}：${message}${targetSuffix}`;
}
