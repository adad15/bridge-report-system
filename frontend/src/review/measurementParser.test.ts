import { describe, expect, it } from "vitest";

import { parseMeasurements } from "./measurementParser";

// 用例镜像 tools-python/tests/importers/test_measurements.py，确保同样的 measurement_text
// 在前端和 Python 工具服务里解析出同样的 Measurement[]（不含 warnings，见 measurementParser.ts 顶部说明）。
describe("parseMeasurements", () => {
  it("parses L= and W= length/width measurements in source order", () => {
    const measurements = parseMeasurements("L=0.8m，W=0.12mm");

    expect(measurements.map((item) => item.dimension_type)).toEqual(["长度", "宽度"]);
    expect(measurements[0]).toEqual({ dimension_type: "长度", value_type: "single", value: 0.8, minimum_value: null, maximum_value: null, unit: "m", is_approximate: false, source_text: "L=0.8m" });
    expect(measurements[1]).toEqual({ dimension_type: "宽度", value_type: "single", value: 0.12, minimum_value: null, maximum_value: null, unit: "mm", is_approximate: false, source_text: "W=0.12mm" });
  });

  it.each(["0.5~4.0m", "0.5～4.0m", "15至20m"])(
    "preserves both endpoints for range %s",
    (sourceText) => {
      const [measurement] = parseMeasurements(sourceText);

      expect(measurement).toMatchObject({
        dimension_type: "长度",
        value_type: "range",
        value: null,
        minimum_value: sourceText.startsWith("15") ? 15 : 0.5,
        maximum_value: sourceText.startsWith("15") ? 20 : 4,
        unit: "m",
        is_approximate: false,
        source_text: sourceText,
      });
    },
  );

  it("binds Chinese range labels to their dimension types", () => {
    const measurements = parseMeasurements(
      "多条纵、横向裂缝，长度范围：0.5～4.0m，宽度范围：0.5～1.0cm，面积范围：1～2m²，间距范围：10～20cm",
    );

    expect(measurements.map((item) => item.dimension_type)).toEqual([
      "长度", "宽度", "面积", "间距",
    ]);
    expect(measurements.map((item) => item.value_type)).toEqual([
      "range", "range", "range", "range",
    ]);
    expect(measurements[0]).toMatchObject({ minimum_value: 0.5, maximum_value: 4, unit: "m" });
    expect(measurements[1]).toMatchObject({ minimum_value: 0.5, maximum_value: 1, unit: "cm" });
    expect(measurements[2]).toMatchObject({ minimum_value: 1, maximum_value: 2, unit: "m2" });
    expect(measurements[3]).toMatchObject({ minimum_value: 10, maximum_value: 20, unit: "cm" });
  });

  it("keeps approximate single values and Chinese labels without separators", () => {
    expect(parseMeasurements("总面积约1.0m²")).toEqual([
      {
        dimension_type: "总面积",
        value_type: "single",
        value: 1,
        minimum_value: null,
        maximum_value: null,
        unit: "m2",
        is_approximate: true,
        source_text: "总面积约1.0m²",
      },
    ]);
    expect(parseMeasurements("长度20.0m")[0]).toMatchObject({
      dimension_type: "长度",
      value_type: "single",
      value: 20,
      is_approximate: false,
    });
  });

  it("parses S=/D= area+spacing and a trailing count expression", () => {
    const measurements = parseMeasurements("S=0.3m2，D=0.15m，3处");

    expect(measurements.map((item) => item.dimension_type)).toEqual(["面积", "间距", "数量"]);
    expect(measurements[0].unit).toBe("m2");
    expect(measurements[1].unit).toBe("m");
    expect(measurements[2].unit).toBe("处");
    expect(measurements[2].value).toBe(3);
  });

  it("returns an empty array for unstable numeric text that cannot be structured", () => {
    expect(parseMeasurements("局部破损，约20左右")).toEqual([]);
  });

  it("ignores descriptive leftover text once L/W/D are parsed out of a mixed sentence", () => {
    const measurements = parseMeasurements("多条横向裂缝,L=1.2m,W=0.15m，间距D=0.15m");

    expect(measurements.map((item) => item.dimension_type)).toEqual(["长度", "宽度", "间距"]);
    expect(measurements[0]).toMatchObject({ value: 1.2, unit: "m" });
  });

  it("preserves source order when a count expression appears before a dimension", () => {
    const measurements = parseMeasurements("3处，L=0.8m");

    expect(measurements.map((item) => item.dimension_type)).toEqual(["数量", "长度"]);
  });

  it("parses an S=a×b area-product expression and skips the overlapping dimension match", () => {
    const measurements = parseMeasurements("1处蜂窝、麻面，S=0.6×0.1m²");

    expect(measurements.map((item) => item.dimension_type)).toEqual(["数量", "面积"]);
    expect(measurements[1]).toEqual({ dimension_type: "面积", value_type: "single", value: 0.06, minimum_value: null, maximum_value: null, unit: "m2", is_approximate: false, source_text: "S=0.6×0.1m²" });
  });

  it("parses Chinese-label length and total-area expressions using the full-width colon", () => {
    const lengthMeasurements = parseMeasurements("勾缝砂浆脱落,长度：5m");
    const areaMeasurements = parseMeasurements("混凝土剥落，破损掉角,总面积：1m²");

    expect(lengthMeasurements).toEqual([{ dimension_type: "长度", value_type: "single", value: 5, minimum_value: null, maximum_value: null, unit: "m", is_approximate: false, source_text: "长度：5m" }]);
    expect(areaMeasurements).toEqual([{ dimension_type: "总面积", value_type: "single", value: 1, minimum_value: null, maximum_value: null, unit: "m2", is_approximate: false, source_text: "总面积：1m²" }]);
  });

  it("returns an empty array for plain descriptive text with no measurement hint", () => {
    expect(parseMeasurements("基本完好")).toEqual([]);
  });

  it("returns an empty array for null or empty input", () => {
    expect(parseMeasurements(null)).toEqual([]);
    expect(parseMeasurements("")).toEqual([]);
  });
});
