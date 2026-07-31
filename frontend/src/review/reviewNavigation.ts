function encoded(value: string): string {
  return encodeURIComponent(value);
}

// 供滚动定位使用的稳定 DOM id。病害与照片列表、未关联照片面板和实际构件字段各自
// 打上标记，系统评定分区点问题时据此跳到对应对象。
export function reviewTargetId(
  kind: "defect" | "defect-field" | "photo" | "unlinked-photo" | "unlinked-photos",
  candidateId: string,
  field?: string
): string {
  const suffix = field ? `-${encoded(field)}` : "";
  return `review-target-${kind}-${encoded(candidateId)}${suffix}`;
}
