import { describe, expect, it } from "vitest";

import type { OverallRating, Ratings, StructurePartRating, EvaluationPartRating } from "../../contracts/annualInspection";
import type { AttentionItem } from "../grouping";
import { formatAttentionItem, ratingRows } from "./displayHelpers";

function makeOverallRating(overrides: Partial<OverallRating> = {}): OverallRating {
  return { total_score: 92, overall_grade: "1类", source_ref: {}, confidence: 0.9, review_status: "待确认", ...overrides };
}

function makeStructurePartRating(overrides: Partial<StructurePartRating> = {}): StructurePartRating {
  return {
    structure_part: "上部结构",
    structure_score: 90,
    weight: 0.5,
    grade: "1类",
    source_ref: {},
    confidence: 0.9,
    review_status: "待确认",
    ...overrides,
  };
}

function makeEvaluationPartRating(overrides: Partial<EvaluationPartRating> = {}): EvaluationPartRating {
  return {
    structure_part: "上部结构",
    category_no: 1,
    evaluation_part: "支座",
    part_score: 95,
    score_rows: [],
    source_ref: {},
    confidence: 0.9,
    review_status: "待确认",
    ...overrides,
  };
}

function makeRatings(overrides: Partial<Ratings> = {}): Ratings {
  return {
    overall: makeOverallRating(),
    structure_parts: [],
    evaluation_parts: [],
    component_ratings: [],
    warnings: [],
    ...overrides,
  };
}

describe("formatAttentionItem", () => {
  it("formats a defect-kind item with kind, candidateId and message", () => {
    const item: AttentionItem = { kind: "defect", candidateId: "defect_0001", message: "构件名称疑似缺失。", severity: "warning" };

    expect(formatAttentionItem(item)).toBe("[defect] defect_0001: 构件名称疑似缺失。");
  });

  it("formats a photo-kind item", () => {
    const item: AttentionItem = { kind: "photo", candidateId: "photo_0002", message: "未关联病害。", severity: "error" };

    expect(formatAttentionItem(item)).toBe("[photo] photo_0002: 未关联病害。");
  });

  it("formats a rating-kind item", () => {
    const item: AttentionItem = { kind: "rating", candidateId: "ratings.overall", message: "全桥评分待人工复核。", severity: "warning" };

    expect(formatAttentionItem(item)).toBe("[rating] ratings.overall: 全桥评分待人工复核。");
  });

  it("formats an import-kind item with info severity (severity itself is not part of the string)", () => {
    const item: AttentionItem = { kind: "import", candidateId: "unknown_9999", message: "无法定位到具体候选。", severity: "info" };

    expect(formatAttentionItem(item)).toBe("[import] unknown_9999: 无法定位到具体候选。");
  });
});

describe("ratingRows", () => {
  it("flattens overall + structure_parts + evaluation_parts, overall first, in array order", () => {
    const ratings = makeRatings({
      structure_parts: [makeStructurePartRating({ structure_part: "上部结构" })],
      evaluation_parts: [makeEvaluationPartRating({ evaluation_part: "支座" })],
    });

    const rows = ratingRows(ratings);

    expect(rows).toHaveLength(3);
    expect(rows[0].level).toBe("全桥");
    expect(rows[1].level).toBe("结构分部");
    expect(rows[2].level).toBe("评价部件");
  });

  it("overall row carries total_score/overall_grade and target 'overall'", () => {
    const ratings = makeRatings({ overall: makeOverallRating({ total_score: 88, overall_grade: "2类", review_status: "已修改" }) });

    const [row] = ratingRows(ratings);

    expect(row).toMatchObject({ level: "全桥", name: "全桥", score: 88, grade: "2类", weight: null, status: "已修改", target: "overall" });
  });

  it("structure part row carries score/grade/weight and target { part }", () => {
    const ratings = makeRatings({
      structure_parts: [makeStructurePartRating({ structure_part: "下部结构", structure_score: 85, weight: 0.3, grade: "2类" })],
    });

    const row = ratingRows(ratings)[1];

    expect(row).toMatchObject({
      level: "结构分部",
      name: "下部结构",
      score: 85,
      grade: "2类",
      weight: 0.3,
      target: { part: "下部结构" },
    });
  });

  it("evaluation part row has null grade and null weight, target { evaluation: index }", () => {
    const ratings = makeRatings({
      evaluation_parts: [
        makeEvaluationPartRating({ evaluation_part: "支座", part_score: 80 }),
        makeEvaluationPartRating({ evaluation_part: "伸缩缝", part_score: 70 }),
      ],
    });

    const rows = ratingRows(ratings);
    const secondEvaluationRow = rows[1];
    const thirdEvaluationRow = rows[2];

    expect(secondEvaluationRow).toMatchObject({ level: "评价部件", name: "支座", score: 80, grade: null, weight: null, target: { evaluation: 0 } });
    expect(thirdEvaluationRow).toMatchObject({ level: "评价部件", name: "伸缩缝", score: 70, grade: null, weight: null, target: { evaluation: 1 } });
  });

  it("returns just the overall row when there are no structure_parts/evaluation_parts", () => {
    const rows = ratingRows(makeRatings());

    expect(rows).toHaveLength(1);
  });
});
