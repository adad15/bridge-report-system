// 规范原文库（设计 docs/superpowers/specs/2026-10-09-standard-documents-library-design.md）。
//
// 和 standardsApi.ts 不是一回事：那边是本系统的结构化规则包（「规则管理」），
// 这边是规范原文 PDF 及其版本（「规范管理」）。

import { ApiError, request } from "./apiClient";

export type StandardVersionKind = "official" | "draft_comment" | "draft_approval";

/** 后端按日期推导的状态，前端不再重复推一遍。 */
export type StandardVersionStatus = "current" | "upcoming" | "withdrawn" | "draft_comment" | "draft_approval";

export interface StandardVersion {
  id: string;
  label: string;
  code: string;
  kind: StandardVersionKind;
  status: StandardVersionStatus;
  nature: string | null;
  issuer: string | null;
  editor: string | null;
  publish_date: string | null;
  effective_date: string | null;
  /** 生效的废止日期：手工值，或被后续正式版替代时推算出来的值。 */
  withdrawn_date: string | null;
  withdrawn_date_source: "manual" | "replacement" | null;
  announcement: string | null;
  source: string | null;
  note: string | null;
  replaced_by_version_id: string | null;
  replaces_version_id: string | null;
  file: { original_name: string; size_bytes: number };
  content_path: string;
  uploaded_by: string | null;
  uploaded_at: string;
  updated_at: string;
}

export interface StandardDocument {
  id: string;
  name: string;
  english_name: string | null;
  category: string;
  default_version_id: string | null;
  /** 按版本日期从新到旧。 */
  versions: StandardVersion[];
}

export interface StandardDocumentInput {
  name: string;
  english_name: string | null;
  category: string;
}

export interface StandardVersionInput {
  label: string;
  code: string;
  kind: StandardVersionKind;
  nature: string | null;
  issuer: string | null;
  editor: string | null;
  publish_date: string | null;
  effective_date: string | null;
  withdrawn_date: string | null;
  announcement: string | null;
  source: string | null;
  note: string | null;
}

/** 与后端全局请求体上限一致（`word_upload_max_bytes`，默认 256 MB），前端先拦一道。 */
export const STANDARD_PDF_MAX_BYTES = 256 * 1024 * 1024;

const JSON_HEADERS = { "Content-Type": "application/json" };

function documentUrl(baseUrl: string, documentId: string): string {
  return `${baseUrl}/api/standard-documents/${encodeURIComponent(documentId)}`;
}

function versionUrl(baseUrl: string, versionId: string): string {
  return `${baseUrl}/api/standard-document-versions/${encodeURIComponent(versionId)}`;
}

export async function fetchStandardDocuments(baseUrl: string): Promise<StandardDocument[]> {
  const body = await request<{ documents: StandardDocument[] }>(`${baseUrl}/api/standard-documents`);
  return body.documents;
}

export async function createStandardDocument(
  baseUrl: string,
  document: StandardDocumentInput,
  version: StandardVersionInput,
  file: File,
): Promise<StandardDocument> {
  const form = new FormData();
  form.append("metadata", JSON.stringify({ document, version }));
  form.append("file", file);
  const body = await request<{ document: StandardDocument }>(`${baseUrl}/api/standard-documents`, {
    method: "POST",
    body: form,
  });
  return body.document;
}

export async function updateStandardDocument(
  baseUrl: string,
  documentId: string,
  document: StandardDocumentInput,
): Promise<StandardDocument> {
  const body = await request<{ document: StandardDocument }>(documentUrl(baseUrl, documentId), {
    method: "PATCH",
    headers: JSON_HEADERS,
    body: JSON.stringify(document),
  });
  return body.document;
}

export async function deleteStandardDocument(baseUrl: string, documentId: string): Promise<void> {
  await request(documentUrl(baseUrl, documentId), { method: "DELETE" });
}

export async function addStandardVersion(
  baseUrl: string,
  documentId: string,
  version: StandardVersionInput,
  file: File,
): Promise<StandardDocument> {
  const form = new FormData();
  form.append("metadata", JSON.stringify(version));
  form.append("file", file);
  const body = await request<{ document: StandardDocument }>(`${documentUrl(baseUrl, documentId)}/versions`, {
    method: "POST",
    body: form,
  });
  return body.document;
}

export async function updateStandardVersion(
  baseUrl: string,
  versionId: string,
  version: StandardVersionInput,
): Promise<StandardDocument> {
  const body = await request<{ document: StandardDocument }>(versionUrl(baseUrl, versionId), {
    method: "PATCH",
    headers: JSON_HEADERS,
    body: JSON.stringify(version),
  });
  return body.document;
}

export async function replaceStandardVersionFile(
  baseUrl: string,
  versionId: string,
  file: File,
): Promise<StandardDocument> {
  const form = new FormData();
  form.append("file", file);
  const body = await request<{ document: StandardDocument }>(`${versionUrl(baseUrl, versionId)}/file`, {
    method: "PUT",
    body: form,
  });
  return body.document;
}

export async function deleteStandardVersion(baseUrl: string, versionId: string): Promise<StandardDocument> {
  const body = await request<{ document: StandardDocument }>(versionUrl(baseUrl, versionId), { method: "DELETE" });
  return body.document;
}

/**
 * 原文地址。内容接口不鉴权：iframe 带不上 Authorization 头。
 *
 * 带上 updated_at：替换原文后地址跟着变，iframe 和浏览器缓存都会取新文件。
 * page 拼成 `#page=N`，浏览器自带的 PDF 阅读器据此跳页。
 */
export function standardVersionContentUrl(
  baseUrl: string,
  version: Pick<StandardVersion, "content_path" | "updated_at">,
  options: { download?: boolean; page?: number } = {},
): string {
  const base = baseUrl.replace(/\/+$/, "");
  const query = new URLSearchParams({ v: version.updated_at });
  if (options.download) query.set("download", "1");
  const fragment = options.page ? `#page=${options.page}` : "";
  return `${base}${version.content_path}?${query.toString()}${fragment}`;
}

export function standardDocumentsError(error: unknown): string {
  if (!(error instanceof ApiError)) return "操作失败，请稍后重试。";
  // drogon 对超大请求直接回 413，没有 JSON 体，apiClient 只能给出这个中性错误码。
  if (error.code === "unrecognized_error_response" && error.message.includes("413")) return "文件超过了上传上限。";
  const messages: Record<string, string> = {
    forbidden: "只有管理员可以维护规范。",
    standard_file_not_pdf: "只能上传 PDF 文件。",
    standard_file_required: "请选择要上传的 PDF 文件。",
    standard_document_name_taken: "已有同名规范。要给它加版本，请选中这部规范后点「上传新版本」。",
    standard_version_label_taken: "这部规范里已有同名版本。",
    standard_version_effective_date_taken: "这部规范里已有同一实施日期的正式版本。",
    standard_document_not_found: "规范不存在或已被删除，请刷新。",
    standard_version_not_found: "版本不存在或已被删除，请刷新。",
    standard_document_commit_uncertain: "保存结果暂时无法确认，请刷新页面核实。",
  };
  // 其余错误码（含 standard_document_invalid 的逐项说明）用后端给的文案。
  return messages[error.code] ?? error.message;
}
