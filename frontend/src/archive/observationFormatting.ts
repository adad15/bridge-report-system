import type { ArchiveMeasurement } from "../api/componentArchiveApi";

// 年度观测尺寸的展示格式：区间显示两个端点，单值显示数值，未识别尺寸留空。
export function formatArchiveMeasurementValue(measurement: ArchiveMeasurement): string {
  const prefix = measurement.is_approximate ? "约" : "";
  const unit = measurement.unit ?? "";
  if (
    measurement.value_type === "range" &&
    measurement.minimum_value !== null &&
    measurement.maximum_value !== null
  ) {
    return `（${prefix}${measurement.minimum_value}~${measurement.maximum_value}${unit}）`;
  }
  if (measurement.numeric_value !== null) {
    return `（${prefix}${measurement.numeric_value}${unit}）`;
  }
  return "";
}
