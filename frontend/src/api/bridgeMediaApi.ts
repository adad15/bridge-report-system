// 桥梁图件：报告 §1.1 用的地理位置图、桥型布置图、横断面图与三张桥梁照片。
//
// 图件属于桥本身，不属于哪一年。一个槽位一张图，换图即覆盖。

import { ApiError, request } from "./apiClient";

export type BridgeMediaSlot =
  | "LOCATION_MAP"
  | "LAYOUT_DRAWING"
  | "CROSS_SECTION"
  | "OVERVIEW_PHOTO"
  | "DECK_PHOTO"
  | "UNDERSIDE_PHOTO";

/**
 * 槽位清单与后端、报告契约共用同一批代码，次序就是报告里的图号次序。
 *
 * `group` 对应报告里的两条编号：地理位置图和示意图共用图号，照片另编。
 * `name` 是放在分组里时的短名：「照片」那一组里不必再写一遍「照片」。
 */
export const BRIDGE_MEDIA_SLOTS: Array<{
  slot: BridgeMediaSlot;
  label: string;
  name: string;
  group: "figure" | "photo";
}> = [
  { slot: "LOCATION_MAP", label: "地理位置图", name: "地理位置图", group: "figure" },
  { slot: "LAYOUT_DRAWING", label: "桥型布置图", name: "桥型布置图", group: "figure" },
  { slot: "CROSS_SECTION", label: "横断面图", name: "横断面图", group: "figure" },
  { slot: "OVERVIEW_PHOTO", label: "桥梁全貌照片", name: "桥梁全貌", group: "photo" },
  { slot: "DECK_PHOTO", label: "桥面照片", name: "桥面", group: "photo" },
  { slot: "UNDERSIDE_PHOTO", label: "桥下照片", name: "桥下", group: "photo" },
];

export interface BridgeMedia {
  id: string;
  bridge_id: string;
  slot: BridgeMediaSlot;
  slot_label: string;
  original_file_name: string;
  file_extension: string;
  file_size_bytes: number;
  source: "人工上传" | "按坐标生成";
  created_at: string;
  updated_at: string;
}

/**
 * 图片地址。
 *
 * 带上更新时间：换图后地址不变，不加这一段浏览器会一直显示缓存里的旧图。
 * 内容接口不鉴权，和病害照片一致——<img src> 带不上 Authorization 头。
 */
export function bridgeMediaContentUrl(baseUrl: string, media: BridgeMedia): string {
  const base = baseUrl.replace(/\/+$/, "");
  return `${base}/api/bridge-media/${encodeURIComponent(media.id)}/content?v=${encodeURIComponent(media.updated_at)}`;
}

export async function fetchBridgeMedia(baseUrl: string, bridgeId: string): Promise<BridgeMedia[]> {
  const body = await request<{ media: BridgeMedia[] }>(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/media`
  );
  return body.media;
}

export async function uploadBridgeMedia(
  baseUrl: string,
  bridgeId: string,
  slot: BridgeMediaSlot,
  file: File
): Promise<BridgeMedia> {
  const form = new FormData();
  form.append("file", file);
  const body = await request<{ media: BridgeMedia }>(
    `${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/media/${slot}`,
    { method: "POST", body: form }
  );
  return body.media;
}

export async function deleteBridgeMedia(
  baseUrl: string,
  bridgeId: string,
  slot: BridgeMediaSlot
): Promise<void> {
  await request(`${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/media/${slot}`, {
    method: "DELETE",
  });
}

export function bridgeMediaError(error: unknown): string {
  if (!(error instanceof ApiError)) return "操作失败，请稍后重试。";
  const messages: Record<string, string> = {
    forbidden: "只有管理员可以修改图件。",
    bridge_media_too_large: "图片太大，超过了上传上限。",
    bridge_media_invalid: "这个文件不是受支持的图片。",
  };
  // 其余错误码的文案由后端给，它知道刚才哪一步没成。
  return messages[error.code] ?? error.message;
}
