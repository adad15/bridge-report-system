import type { Measurement } from "../contracts/annualInspection";

// 与 tools-python/bridge_report_tools/importers/measurements.py 保持规则一致。
const DIMENSION_LABELS: Record<string, string> = { L: "长度", W: "宽度", S: "面积", A: "面积", D: "间距" };
const CHINESE_DIMENSION_LABELS: Record<string, string> = {
  长度: "长度", 宽度: "宽度", 面积: "面积", 总面积: "总面积", 间距: "间距",
};
const DIMENSION_PATTERN = /(?<label>[LWSAD])\s*[=:：]\s*(?<approx>约|大约|约为)?\s*(?<value>\d+(?:\.\d+)?)\s*(?<unit>m2|m²|㎡|mm|cm|m)/gi;
const CHINESE_DIMENSION_PATTERN = /(?<label>总面积|面积|长度|宽度|间距)(?:范围)?\s*[=:：]?\s*(?<approx>约|大约|约为)?\s*(?<value>\d+(?:\.\d+)?)\s*(?<unit>m2|m²|㎡|mm|cm|m)/gi;
const RANGE_PATTERN = /(?:(?<label>总面积|面积|长度|宽度|间距|[LWSAD])(?:范围)?\s*[=:：]?\s*)?(?<approx>约|大约|约为)?\s*(?<minimum>\d+(?:\.\d+)?)\s*(?:~|～|至)\s*(?<maximum>\d+(?:\.\d+)?)\s*(?<unit>m2|m²|㎡|mm|cm|m)/gi;
const APPROXIMATE_SINGLE_PATTERN = /(?<approx>约|大约|约为)\s*(?<value>\d+(?:\.\d+)?)\s*(?<unit>m2|m²|㎡|mm|cm|m)/gi;
const AREA_PRODUCT_PATTERN = /(?:(?<label>[SA])\s*[=:：]\s*)?(?<first>\d+(?:\.\d+)?)\s*(?<firstUnit>mm|cm|m)?\s*[×xX*]\s*(?<second>\d+(?:\.\d+)?)\s*(?<secondUnit>m2|m²|㎡|mm2|mm²|cm2|cm²|mm|cm|m)/gi;
const COUNT_PATTERN = /(?<value>\d+(?:\.\d+)?)\s*(?<unit>处|条|个|块)/g;

type Span = readonly [number, number];
interface MeasurementMatch { span: Span; measurement: Measurement; }

function normalizeUnit(unit: string): string {
  if (unit === "m²" || unit === "㎡") return "m2";
  if (unit === "cm²") return "cm2";
  if (unit === "mm²") return "mm2";
  return unit;
}
function areaUnit(unit: string): string {
  const normalized = normalizeUnit(unit);
  return ["m2", "cm2", "mm2"].includes(normalized) ? normalized : `${normalized}2`;
}
function dimensionTypeFor(label: string | undefined, unit: string): string {
  if (label) {
    const mapped = DIMENSION_LABELS[label.toUpperCase()] ?? CHINESE_DIMENSION_LABELS[label];
    if (mapped) return mapped;
  }
  return normalizeUnit(unit).endsWith("2") ? "面积" : "长度";
}
function spanOf(match: RegExpMatchArray): Span {
  const start = match.index ?? 0;
  return [start, start + match[0].length];
}
function overlaps(span: Span, existing: Span[]): boolean {
  return existing.some(([start, end]) => span[0] < end && span[1] > start);
}
function singleMeasurement(dimensionType: string, value: number, unit: string, sourceText: string, isApproximate = false): Measurement {
  return {
    dimension_type: dimensionType, value_type: "single", value,
    minimum_value: null, maximum_value: null, unit,
    is_approximate: isApproximate, source_text: sourceText,
  };
}
function addMatch(matches: MeasurementMatch[], match: RegExpMatchArray, measurement: Measurement): void {
  const span = spanOf(match);
  if (!overlaps(span, matches.map((item) => item.span))) matches.push({ span, measurement });
}

export function parseMeasurements(text: string | null | undefined): Measurement[] {
  if (!text) return [];
  const matches: MeasurementMatch[] = [];

  for (const match of text.matchAll(RANGE_PATTERN)) {
    const groups = match.groups as { label?: string; approx?: string; minimum: string; maximum: string; unit: string };
    const unit = normalizeUnit(groups.unit);
    addMatch(matches, match, {
      dimension_type: dimensionTypeFor(groups.label, unit), value_type: "range", value: null,
      minimum_value: Number(groups.minimum), maximum_value: Number(groups.maximum), unit,
      is_approximate: groups.approx !== undefined, source_text: match[0],
    });
  }
  for (const match of text.matchAll(DIMENSION_PATTERN)) {
    const groups = match.groups as { label: string; approx?: string; value: string; unit: string };
    addMatch(matches, match, singleMeasurement(
      DIMENSION_LABELS[groups.label.toUpperCase()], Number(groups.value), normalizeUnit(groups.unit), match[0], groups.approx !== undefined,
    ));
  }
  for (const match of text.matchAll(CHINESE_DIMENSION_PATTERN)) {
    const groups = match.groups as { label: string; approx?: string; value: string; unit: string };
    addMatch(matches, match, singleMeasurement(
      CHINESE_DIMENSION_LABELS[groups.label], Number(groups.value), normalizeUnit(groups.unit), match[0], groups.approx !== undefined,
    ));
  }
  for (const match of text.matchAll(APPROXIMATE_SINGLE_PATTERN)) {
    const groups = match.groups as { value: string; unit: string };
    const unit = normalizeUnit(groups.unit);
    addMatch(matches, match, singleMeasurement(dimensionTypeFor(undefined, unit), Number(groups.value), unit, match[0], true));
  }
  for (const match of text.matchAll(AREA_PRODUCT_PATTERN)) {
    const groups = match.groups as { first: string; second: string; secondUnit: string };
    addMatch(matches, match, singleMeasurement(
      "面积", Math.round(Number(groups.first) * Number(groups.second) * 1e6) / 1e6,
      areaUnit(groups.secondUnit), match[0],
    ));
  }
  for (const match of text.matchAll(COUNT_PATTERN)) {
    const groups = match.groups as { value: string; unit: string };
    addMatch(matches, match, singleMeasurement("数量", Number(groups.value), groups.unit, match[0]));
  }
  matches.sort((a, b) => a.span[0] - b.span[0]);
  return matches.map((item) => item.measurement);
}
