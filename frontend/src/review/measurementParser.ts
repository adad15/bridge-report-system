import type { Measurement } from "../contracts/annualInspection";

// 本文件必须与 tools-python/bridge_report_tools/importers/measurements.py 的 parse_measurements
// 保持规则一致（同样的 measurement_text 必须产出同样的 Measurement[]）。任何规则变更都要同步改两处。
//
// 第一版范围只做原文档要求的五族模式 + 重叠去重 + 按出现顺序排序：
//   1. 字母量纲（L=/W=/S=/A=/D= + 数字 + 单位）
//   2. 中文量纲（长度=/宽度=/面积=/总面积=/间距= + 数字 + 单位）
//   3. 面积乘积表达式（可选 S=/A= + 数字 [单位]? × 数字 单位）
//   4. 数量（数字 + 处/条/个/块）
//   5. 上述四族按起始位置排序后的最终顺序
//
// Python 版本额外返回“未能稳定结构化”的低置信度 warning（should_warn 逻辑）。计划书中
// parseMeasurements 的签名只要求返回 Measurement[]，grouping.ts 用自己的启发式正则单独判断
// “有数值线索但未结构化”，因此本文件不再移植 should_warn／低置信度 warning 分支，保持职责单一。

const DIMENSION_LABELS: Record<string, string> = {
  L: "长度",
  W: "宽度",
  S: "面积",
  A: "面积",
  D: "间距",
};

const CHINESE_DIMENSION_LABELS: Record<string, string> = {
  长度: "长度",
  宽度: "宽度",
  面积: "面积",
  总面积: "总面积",
  间距: "间距",
};

// JS 正则的具名捕获组语义与 Python re 模块一致；用 g 标志配合 matchAll 实现 finditer 效果。
// matchAll 内部会克隆正则对象再迭代，不会污染下面这些模块级常量的 lastIndex，可安全跨调用复用。
const DIMENSION_PATTERN = /(?<label>[LWSAD])\s*[=:：]\s*(?<value>\d+(?:\.\d+)?)\s*(?<unit>m2|m²|㎡|mm|cm|m)/gi;

const CHINESE_DIMENSION_PATTERN =
  /(?<label>总面积|面积|长度|宽度|间距)\s*[=:：]\s*(?<value>\d+(?:\.\d+)?)\s*(?<unit>m2|m²|㎡|mm|cm|m)/gi;

const AREA_PRODUCT_PATTERN =
  /(?:(?<label>[SA])\s*[=:：]\s*)?(?<first>\d+(?:\.\d+)?)\s*(?<firstUnit>mm|cm|m)?\s*[×xX*]\s*(?<second>\d+(?:\.\d+)?)\s*(?<secondUnit>m2|m²|㎡|mm2|mm²|cm2|cm²|mm|cm|m)/gi;

const COUNT_PATTERN = /(?<value>\d+(?:\.\d+)?)\s*(?<unit>处|条|个|块)/g;

type Span = readonly [number, number];

interface MeasurementMatch {
  span: Span;
  measurement: Measurement;
}

function normalizeUnit(unit: string): string {
  if (unit === "m²" || unit === "㎡") {
    return "m2";
  }
  if (unit === "cm²") {
    return "cm2";
  }
  if (unit === "mm²") {
    return "mm2";
  }
  return unit;
}

function areaUnit(unit: string): string {
  const normalized = normalizeUnit(unit);
  if (normalized === "m2" || normalized === "cm2" || normalized === "mm2") {
    return normalized;
  }
  return `${normalized}2`;
}

function spanOf(match: RegExpMatchArray): Span {
  const start = match.index ?? 0;
  return [start, start + match[0].length] as const;
}

function overlaps(span: Span, existingSpans: Span[]): boolean {
  const [start, end] = span;
  return existingSpans.some(([existingStart, existingEnd]) => start < existingEnd && end > existingStart);
}

// 六位小数四舍五入，对应 Python round(value, 6)；用于面积乘积表达式的 first*second。
function round6(value: number): number {
  return Math.round(value * 1e6) / 1e6;
}

/**
 * 纯函数：把病害候选的尺寸原文解析为结构化 Measurement[]。
 *
 * 解析不稳定（无法结构化）时返回空数组，不抛错——与 Python parse_measurements 的
 * “测量值部分”行为一致（低置信度 warning 由 grouping.ts 单独处理，见文件顶部说明）。
 */
export function parseMeasurements(text: string | null | undefined): Measurement[] {
  if (!text) {
    return [];
  }

  const matches: MeasurementMatch[] = [];

  for (const match of text.matchAll(DIMENSION_PATTERN)) {
    const groups = match.groups as { label: string; value: string; unit: string };
    const label = groups.label.toUpperCase();
    matches.push({
      span: spanOf(match),
      measurement: {
        dimension_type: DIMENSION_LABELS[label],
        value: parseFloat(groups.value),
        unit: normalizeUnit(groups.unit),
        source_text: match[0].replace(/：/g, "="),
      },
    });
  }

  for (const match of text.matchAll(CHINESE_DIMENSION_PATTERN)) {
    const span = spanOf(match);
    if (overlaps(span, matches.map((item) => item.span))) {
      continue;
    }
    const groups = match.groups as { label: string; value: string; unit: string };
    matches.push({
      span,
      measurement: {
        dimension_type: CHINESE_DIMENSION_LABELS[groups.label],
        value: parseFloat(groups.value),
        unit: normalizeUnit(groups.unit),
        source_text: match[0].replace(/：/g, "="),
      },
    });
  }

  for (const match of text.matchAll(AREA_PRODUCT_PATTERN)) {
    const span = spanOf(match);
    if (overlaps(span, matches.map((item) => item.span))) {
      continue;
    }
    const groups = match.groups as { first: string; second: string; secondUnit: string };
    const first = parseFloat(groups.first);
    const second = parseFloat(groups.second);
    matches.push({
      span,
      measurement: {
        dimension_type: "面积",
        value: round6(first * second),
        unit: areaUnit(groups.secondUnit),
        source_text: match[0].replace(/：/g, "="),
      },
    });
  }

  for (const match of text.matchAll(COUNT_PATTERN)) {
    const span = spanOf(match);
    if (overlaps(span, matches.map((item) => item.span))) {
      continue;
    }
    const groups = match.groups as { value: string; unit: string };
    matches.push({
      span,
      measurement: {
        dimension_type: "数量",
        value: parseFloat(groups.value),
        unit: groups.unit,
        source_text: match[0],
      },
    });
  }

  matches.sort((a, b) => a.span[0] - b.span[0]);
  return matches.map((item) => item.measurement);
}
