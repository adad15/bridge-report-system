import { Descriptions } from "antd";

import type { StandardDocument, StandardVersion } from "../api/standardDocumentsApi";
import { VersionStatusTag, isDraft } from "./versionStatus";

function fileSize(bytes: number): string {
  if (bytes >= 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${Math.max(1, Math.round(bytes / 1024))} KB`;
}

/** 基本信息页签（设计 §5.6）。空值显示「—」。 */
export function StandardBasicInfo({ document, version }: { document: StandardDocument; version: StandardVersion }) {
  const text = (value: string | null) => value ?? "—";
  return (
    <Descriptions
      bordered
      column={{ xs: 1, lg: 2 }}
      items={[
        { key: "name", label: "规范名称", children: document.name },
        { key: "english", label: "英文名称", children: text(document.english_name) },
        { key: "code", label: "标准编号", children: version.code },
        { key: "nature", label: "标准性质", children: text(version.nature) },
        { key: "issuer", label: "发布部门", children: text(version.issuer) },
        { key: "editor", label: "主编单位", children: text(version.editor) },
        { key: "publish", label: isDraft(version) ? "稿件日期" : "发布日期", children: text(version.publish_date) },
        { key: "effective", label: "实施日期", children: text(version.effective_date) },
        { key: "withdrawn", label: "废止日期", children: text(version.withdrawn_date) },
        { key: "announcement", label: "公告文号", children: text(version.announcement) },
        { key: "status", label: "状态", children: <VersionStatusTag status={version.status} /> },
        { key: "file", label: "原文", children: `PDF · ${fileSize(version.file.size_bytes)}` },
        { key: "source", label: "原文来源", children: text(version.source) },
        { key: "uploaded", label: "上传", children: `${version.uploaded_by ?? "—"} · ${version.uploaded_at}` },
        { key: "note", label: "备注", children: text(version.note) },
      ]}
    />
  );
}
