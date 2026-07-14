// JTG/T H21-2011 第 4.1.1 条构件技术状况评分纯函数。
// 与 Python bridge_report_tools.scoring.component_score、C++ review/ComponentScore
// 逐行同构，共享夹具 samples/scoring/component_score_cases.json 保证跨语言一致。

export const COMPONENT_SCORE_STANDARD = "JTG/T H21-2011 4.1.1" as const;
export const COMPONENT_SCORE_ROUNDING_SCALE = 2 as const;

export interface ComponentScoreResult {
  /** 未舍入的构件评分，计算全程不得提前舍入。 */
  score: number;
  /** 参与计算的病害扣分，降序排列。 */
  orderedDeductions: number[];
}

/** 两位小数、半数远离零的舍入；禁用语言内建舍入的平/半舍规则。 */
export function roundScoreToTwoDecimals(value: number): number {
  if (value < 0) {
    return -Math.floor(-value * 100.0 + 0.5) / 100.0;
  }
  return Math.floor(value * 100.0 + 0.5) / 100.0;
}

/**
 * 按第 4.1.1 条累计扣分计算构件评分。
 * 输入为空或任一扣分不在 (0, 100] 内时无法计算，返回 null。
 * 输入顺序不影响结果：内部先降序排序再累计；任一 DP=100 时评分为 0。
 */
export function computeComponentScore(deductions: number[]): ComponentScoreResult | null {
  if (deductions.length === 0) {
    return null;
  }
  for (const value of deductions) {
    if (!Number.isFinite(value) || !(value > 0) || value > 100) {
      return null;
    }
  }

  const ordered = [...deductions].sort((left, right) => right - left);
  if (ordered[0] === 100) {
    return { score: 0, orderedDeductions: ordered };
  }

  let total = 0;
  for (let index = 1; index <= ordered.length; index += 1) {
    const deduction = ordered[index - 1];
    const u = index === 1
      ? deduction
      : (deduction / (100.0 * Math.sqrt(index))) * (100.0 - total);
    total += u;
  }
  return { score: 100.0 - total, orderedDeductions: ordered };
}

/**
 * 按两位小数比较来源分与复算分：复算缺失 -> 无法复算；
 * 来源缺失 -> 不一致（必须人工显式处理）；round2 相等 -> 一致，否则 -> 不一致。
 */
export function classifyScoreValidation(
  sourceScore: number | null | undefined,
  calculatedScore: number | null | undefined,
): "一致" | "不一致" | "无法复算" {
  if (calculatedScore === null || calculatedScore === undefined) {
    return "无法复算";
  }
  if (sourceScore === null || sourceScore === undefined) {
    return "不一致";
  }
  if (roundScoreToTwoDecimals(sourceScore) === roundScoreToTwoDecimals(calculatedScore)) {
    return "一致";
  }
  return "不一致";
}
