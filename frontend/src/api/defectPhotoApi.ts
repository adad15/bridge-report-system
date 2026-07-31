// 人工补充照片的上传与删除。Word 抽出的照片不走这两个端点——
// 那种“删除”只是把 linked_defect_candidate_id 改掉，纯本地草稿操作。

import { ApiError, request } from "./apiClient";
import type { PhotoCandidate } from "../contracts/annualInspection";

function photosRoute(baseUrl: string, importRecordId: string, suffix = ""): string {
  const normalizedBaseUrl = baseUrl.replace(/\/+$/, "");
  return `${normalizedBaseUrl}/api/import-records/${encodeURIComponent(importRecordId)}/photos${suffix}`;
}

function lockHeaders(lockToken: string): Headers {
  const headers = new Headers();
  headers.set("X-Edit-Lock-Token", lockToken);
  return headers;
}

/**
 * 上传一张补充照片。
 *
 * 服务端会立刻把候选写进 parsed_result_json，所以不保存直接刷新页面照片仍在；
 * 返回的候选要原样落进本地草稿，避免同一张图在两边各有一份不同的内容。
 */
export async function uploadDefectPhoto(
  baseUrl: string,
  importRecordId: string,
  lockToken: string,
  input: { file: File; defectCandidateId: string; caption: string }
): Promise<PhotoCandidate> {
  const form = new FormData();
  form.append("file", input.file);
  form.append("defect_candidate_id", input.defectCandidateId);
  form.append("caption", input.caption);
  const body = await request<{ photo: PhotoCandidate }>(photosRoute(baseUrl, importRecordId), {
    method: "POST",
    headers: lockHeaders(lockToken),
    body: form,
  });
  return body.photo;
}

/** 删除一张人工上传的照片：草稿、两张文件表与归档文件一起清理。 */
export async function deleteUploadedPhoto(
  baseUrl: string,
  importRecordId: string,
  lockToken: string,
  photoCandidateId: string
): Promise<void> {
  await request<{ deleted: boolean }>(
    photosRoute(baseUrl, importRecordId, `/${encodeURIComponent(photoCandidateId)}`),
    { method: "DELETE", headers: lockHeaders(lockToken) }
  );
}

export function defectPhotoErrorMessage(error: unknown): string {
  if (!(error instanceof ApiError)) return "照片操作失败，请稍后重试。";
  const stable: Record<string, string> = {
    invalid_photo_file: "这个文件不是受支持的图片，或扩展名与内容不符。",
    photo_file_too_large: "照片超过允许的大小上限。",
    defect_candidate_not_found: "目标病害已不在本次导入中，请刷新后重试。",
    import_record_not_editable: "当前导入记录不可编辑，无法增删照片。",
    photo_not_deletable: "Word 抽出的照片不能从这里删除。",
    photo_candidate_not_found: "这张照片已不在本次导入中，请刷新后重试。",
    photo_candidate_conflict: "照片编号被占用，请重试一次。",
    edit_lock_required: "请先获取编辑权后再操作照片。",
  };
  return stable[error.code] ?? error.message;
}
