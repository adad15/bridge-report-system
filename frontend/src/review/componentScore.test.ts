import { readFileSync } from "node:fs";
import { resolve } from "node:path";

import { describe, expect, it } from "vitest";

import {
  classifyScoreValidation,
  computeComponentScore,
  roundScoreToTwoDecimals,
} from "./componentScore";

interface ScoreCase {
  name: string;
  deductions: number[];
  expected_unrounded: number | null;
  expected_rounded: number | null;
  expected_ordered_deductions: number[] | null;
}

function loadCases(): ScoreCase[] {
  // Vitest 的工作目录固定为 frontend/，共享夹具位于仓库根的 samples/scoring/。
  const fixturePath = resolve(process.cwd(), "..", "samples", "scoring", "component_score_cases.json");
  const fixture = JSON.parse(readFileSync(fixturePath, "utf-8")) as {
    standard: string;
    cases: ScoreCase[];
  };
  expect(fixture.standard).toBe("JTG/T H21-2011 4.1.1");
  return fixture.cases;
}

describe("computeComponentScore", () => {
  it("matches every shared fixture case", () => {
    const cases = loadCases();
    expect(cases.length).toBeGreaterThanOrEqual(10);

    for (const scoreCase of cases) {
      const result = computeComponentScore(scoreCase.deductions);
      if (scoreCase.expected_unrounded === null) {
        expect(result, scoreCase.name).toBeNull();
        continue;
      }
      expect(result, scoreCase.name).not.toBeNull();
      expect(Math.abs(result!.score - scoreCase.expected_unrounded), scoreCase.name).toBeLessThan(1e-9);
      expect(roundScoreToTwoDecimals(result!.score), scoreCase.name).toBe(scoreCase.expected_rounded);
      expect(result!.orderedDeductions, scoreCase.name).toEqual(scoreCase.expected_ordered_deductions);
    }
  });

  it("matches the change proposal key values without premature rounding", () => {
    expect(computeComponentScore([35])!.score).toBe(65);

    const two = computeComponentScore([35, 20])!;
    expect(roundScoreToTwoDecimals(two.score)).toBe(55.81);
    const manual = 100.0 - 35.0 - (20.0 / (100.0 * Math.sqrt(2.0))) * 65.0;
    expect(two.score).toBe(manual);

    expect(computeComponentScore([20, 35])!.score).toBe(two.score);
    expect(computeComponentScore([100])!.score).toBe(0);
    expect(computeComponentScore([50, 100])!.score).toBe(0);
  });

  it("rejects empty and out-of-range deductions", () => {
    expect(computeComponentScore([])).toBeNull();
    expect(computeComponentScore([-5])).toBeNull();
    expect(computeComponentScore([150])).toBeNull();
  });

  it("accepts zero deductions without changing the accumulated deduction", () => {
    expect(computeComponentScore([0])!.score).toBe(100);
    expect(computeComponentScore([0, 0])!.score).toBe(100);
    expect(computeComponentScore([35, 0])!.score).toBe(65);
    expect(roundScoreToTwoDecimals(computeComponentScore([35, 20, 0])!.score)).toBe(55.81);
    expect(computeComponentScore([100, 0])!.score).toBe(0);
  });
});

describe("classifyScoreValidation", () => {
  it("classifies missing, matching, and mismatching scores", () => {
    expect(classifyScoreValidation(null, null)).toBe("无法复算");
    expect(classifyScoreValidation(55.81, null)).toBe("无法复算");
    expect(classifyScoreValidation(null, 55.80761184457488)).toBe("不一致");
    expect(classifyScoreValidation(55.81, 55.80761184457488)).toBe("一致");
    expect(classifyScoreValidation(65, 55.80761184457488)).toBe("不一致");
  });
});

describe("roundScoreToTwoDecimals", () => {
  it("rounds half away from zero deterministically", () => {
    expect(roundScoreToTwoDecimals(55.805)).toBe(55.81);
    expect(roundScoreToTwoDecimals(55.8076118445748)).toBe(55.81);
    expect(roundScoreToTwoDecimals(65)).toBe(65);
    expect(roundScoreToTwoDecimals(0.125)).toBe(0.13);
    expect(roundScoreToTwoDecimals(-1.005)).toBe(-roundScoreToTwoDecimals(1.005));
  });
});
