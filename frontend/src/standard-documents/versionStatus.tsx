import { Tag } from "antd";

import type { StandardVersion, StandardVersionKind, StandardVersionStatus } from "../api/standardDocumentsApi";

/** 状态标签的文案与颜色（设计 §5.3）。 */
export const VERSION_STATUS_META: Record<StandardVersionStatus, { label: string; color: string }> = {
  current: { label: "现行", color: "success" },
  upcoming: { label: "即将实施", color: "processing" },
  withdrawn: { label: "已废止", color: "default" },
  draft_comment: { label: "征求意见稿", color: "warning" },
  draft_approval: { label: "报批稿", color: "warning" },
};

export const VERSION_KIND_LABELS: Record<StandardVersionKind, string> = {
  official: "正式版",
  draft_comment: "征求意见稿",
  draft_approval: "报批稿",
};

export function VersionStatusTag({ status }: { status: StandardVersionStatus }) {
  const meta = VERSION_STATUS_META[status];
  return <Tag color={meta.color} style={{ marginInlineEnd: 0 }}>{meta.label}</Tag>;
}

export function isDraft(version: Pick<StandardVersion, "kind">): boolean {
  return version.kind !== "official";
}

/** 头部那一行放不下「中华人民共和国」，去掉前缀。 */
export function shortIssuer(issuer: string | null): string | null {
  return issuer ? issuer.replace(/^中华人民共和国/, "") : null;
}

/** 「2011-09-01 实施」；稿件写「稿件日期 2026-02-11」；都没有返回空。 */
export function versionDateText(version: StandardVersion): string | null {
  if (isDraft(version)) return version.publish_date ? `稿件日期 ${version.publish_date}` : null;
  return version.effective_date ? `${version.effective_date} 实施` : null;
}
