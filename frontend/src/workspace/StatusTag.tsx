import { Tag } from "antd";

import { statusTone, type StatusTone } from "./workspaceState";

const TONE_COLOR: Record<StatusTone, string> = {
  ok: "success",
  warn: "warning",
  danger: "error",
  neutral: "default",
};

/** 业务状态标签：颜色按含义分档，同一状态在各页面颜色一致（设计 §8.5）。 */
export function StatusTag({ status }: { status: string }) {
  return <Tag color={TONE_COLOR[statusTone(status)]}>{status}</Tag>;
}
