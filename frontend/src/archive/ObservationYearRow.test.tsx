import { describe, expect, it } from "vitest";

import { formatArchiveMeasurementValue } from "./ObservationYearRow";

describe("formatArchiveMeasurementValue", () => {
  it("shows both formal range endpoints", () => {
    expect(formatArchiveMeasurementValue({
      measurement_type: "长度", value_type: "range", numeric_value: null,
      minimum_value: 0.5, maximum_value: 4, unit: "m", is_approximate: false,
      raw_text: "0.5~4.0m",
    })).toBe("（0.5~4m）");
  });

  it("shows approximate single values and leaves unstructured rows blank", () => {
    expect(formatArchiveMeasurementValue({
      measurement_type: "面积", value_type: "single", numeric_value: 1,
      minimum_value: null, maximum_value: null, unit: "m2", is_approximate: true,
      raw_text: "约1m²",
    })).toBe("（约1m2）");
    expect(formatArchiveMeasurementValue({
      measurement_type: "未识别尺寸", value_type: null, numeric_value: null,
      minimum_value: null, maximum_value: null, unit: null, is_approximate: false,
      raw_text: "约三处",
    })).toBe("");
  });
});
