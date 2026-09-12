// 桥梁档案里「这座桥本身」那部分的读写。
//
// 报告 §1.1「桥梁概况」的几段叙述、附录2 卡片的十几格，取的都是这一份数据——
// 同一个事实只存一处，不在报告侧另抄一份。
//
// 写是**整体覆盖**：一次提交一整张表，把录错的项清空是正当操作。所以保存前要把
// 当前值全部装进表单，只提交改动的那几项会把其余项抹掉。

import { ApiError, request } from "./apiClient";

/** 值为 null 表示档案里还没录；不是 0，也不是空串。 */
export interface BridgeProfile {
  bridge_id: string;
  bridge_name: string;
  business_code: string | null;
  route_number: string | null;
  route_name: string | null;
  administrative_region: string | null;
  station_mark: string | null;

  bridge_type: string | null;
  bridge_scale: string | null;
  span_combination: string | null;
  bridge_length_m: number | null;
  /** 含人行道的桥面总宽。与 carriageway_width_m 不是一回事。 */
  bridge_width_m: number | null;
  built_year: number | null;

  skew_angle_deg: number | null;
  /** §1.1 印作「桥面净宽」，附录2 第 24 格印作「行车道宽」，同一个量。 */
  carriageway_width_m: number | null;
  /** 单侧人行道宽度。两侧等宽时只录一个数。 */
  sidewalk_width_m: number | null;

  deck_pavement: string | null;
  expansion_joint_type: string | null;
  /** 设伸缩缝的墩号，原样保存录入的写法。 */
  expansion_joint_piers: string | null;
  bearing_type: string | null;

  superstructure_form: string | null;
  girders_per_span: number | null;
  girder_height_m: number | null;

  abutment_form: string | null;
  pier_form: string | null;
  foundation_form: string | null;

  design_load: string | null;
  design_org: string | null;
  construction_org: string | null;
  /** 管养单位。与 supervision_org（监管单位）不是一回事。 */
  maintenance_org: string | null;
  supervision_org: string | null;
}

/** 可编辑的那些键。桥名与状态在桥梁管理里改，不在这张表里。 */
export type BridgeProfileField = Exclude<keyof BridgeProfile, "bridge_id" | "bridge_name">;

/** 提交的是字符串：输入框里留的就是字符串，空串在后端归一化为"没录"。 */
export type BridgeProfileInput = Partial<Record<BridgeProfileField, string>>;

export function fetchBridgeProfile(baseUrl: string, bridgeId: string): Promise<BridgeProfile> {
  return request(`${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/profile`);
}

export function saveBridgeProfile(
  baseUrl: string,
  bridgeId: string,
  input: BridgeProfileInput
): Promise<BridgeProfile> {
  return request(`${baseUrl}/api/bridges/${encodeURIComponent(bridgeId)}/profile`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(input),
  });
}

export function bridgeProfileError(error: unknown): string {
  if (!(error instanceof ApiError)) return "操作失败，请稍后重试。";
  const messages: Record<string, string> = {
    bridge_not_found: "桥梁不存在，可能已经被删除。",
    bridge_profile_measure_out_of_range:
      "梁片数、梁高和宽度必须大于 0，斜交角要在 0 到 180 度之间。",
    forbidden: "只有管理员可以修改桥梁档案。",
  };
  return messages[error.code] ?? error.message;
}
