import { describe, expect, it } from "vitest";

import type {
  BridgeAnnualInspectionData,
  ComponentRatingCandidate,
  DefectCandidate,
  EvaluationPartRating,
  OverallRating,
  PhotoCandidate,
  StructurePartRating,
  WarningItem,
} from "../contracts/annualInspection";
import {
  buildStatistics,
  isNormalComponentRating,
  isNormalDefect,
  isNormalPhoto,
  isNormalRating,
  needsAttention,
} from "./grouping";

function makeWarning(overrides: Partial<WarningItem> = {}): WarningItem {
  return { code: "some_warning", message: "需要人工确认。", severity: "warning", target_candidate_id: null, ...overrides };
}

function makeDefect(overrides: Partial<DefectCandidate> = {}): DefectCandidate {
  return {
    candidate_id: "defect_0001",
    structure_part: "上部结构",
    component_name: "主梁",
    component_number: "1-1#",
    bridge_component_id: "component-1",
    standard_component_category_id: "main-girder",
    resolved_structure_part: "上部结构",
    component_alias: null,
    defect_type: "裂缝",
    defect_location: "跨中",
    defect_description: "横向裂缝",
    quantity_text: null,
    measurement_text: null,
    measurements: [],
    photo_numbers: [],
    group_review_status: "已确认",
    confirmed_missing_photo_numbers: [],
    severity: null,
    remark: null,
    source_ref: {},
    confidence: 0.9,
    review_status: "待确认",
    review_note: null,
    warnings: [],
    ...overrides,
  };
}

function makePhoto(overrides: Partial<PhotoCandidate> = {}): PhotoCandidate {
  return {
    candidate_id: "photo_0001",
    photo_number: "1",
    linked_defect_candidate_id: "defect_0001",
    extracted_file: {
      temporary_file_name: "tmp_0001.jpg",
      original_caption: null,
      archive_relative_path: "photos/tmp_0001.jpg",
    },
    match_status: "已确认",
    source_ref: {},
    confidence: 0.9,
    review_status: "待确认",
    warnings: [],
    ...overrides,
  };
}

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

function makeData(overrides: Partial<BridgeAnnualInspectionData> = {}): BridgeAnnualInspectionData {
  return {
    contract: {
      name: "BridgeAnnualInspectionData",
      version: "2.0",
      generated_at: "2026-07-09T00:00:00+08:00",
      producer: "bridge-report-system",
      parser_name: "test-parser",
      parser_version: "1.0.0",
    },
    import_context: {
      source_type: "软件导出Word",
      file_role: "当前年度检测资料",
      archived_file_system_number: "ARCH-2026-0001",
      import_record_system_number: "IMP-2026-0001",
    },
    bridge_check: { selected_bridge_system_number: "QL-000001", match_status: "匹配", warnings: [] },
    inspection: {
      inspection_year: 2026,
      inspection_date: "2026-05-18",
      report_number: "BG-2026-0001",
      project_name: "测试项目",
      data_role: "当前年度",
    },
    defects: [],
    photos: [],
    ratings: { overall: makeOverallRating(), structure_parts: [], evaluation_parts: [], component_ratings: [], warnings: [] },
    comparison_candidates: [],
    report_text_candidates: [],
    warnings: [],
    errors: [],
    ...overrides,
  };
}

describe("needsAttention", () => {
  it("rule 1 (defect): a defect with a non-empty warnings[] is flagged", () => {
    const data = makeData({ defects: [makeDefect({ warnings: [makeWarning({ message: "构件名称疑似缺失。" })] })] });

    const items = needsAttention(data);

    expect(items).toContainEqual(
      expect.objectContaining({ kind: "defect", candidateId: "defect_0001", message: "构件名称疑似缺失。" })
    );
  });

  it("rule 1 (defect) negative: a defect with no warnings and nothing else wrong is not flagged", () => {
    const data = makeData({ defects: [makeDefect()] });

    expect(needsAttention(data)).toEqual([]);
  });

  it("rule 1 (photo): a photo with a non-empty warnings[] is flagged", () => {
    const data = makeData({ photos: [makePhoto({ warnings: [makeWarning({ message: "置信度偏低。" })] })] });

    const items = needsAttention(data);

    expect(items).toContainEqual(
      expect.objectContaining({ kind: "photo", candidateId: "photo_0001", message: "置信度偏低。" })
    );
  });

  it("rule 1 (photo) negative: a clean linked/confirmed photo with no warnings is not flagged", () => {
    const data = makeData({ photos: [makePhoto()] });

    expect(needsAttention(data)).toEqual([]);
  });

  it("rule 2: a top-level warning with target_candidate_id pointing at a defect is flagged for that defect", () => {
    const data = makeData({
      defects: [makeDefect()],
      warnings: [makeWarning({ message: "导入级提示。", target_candidate_id: "defect_0001" })],
    });

    const items = needsAttention(data);

    expect(items).toContainEqual(
      expect.objectContaining({ kind: "defect", candidateId: "defect_0001", message: "导入级提示。" })
    );
  });

  it("rule 2 negative: a top-level warning with no target_candidate_id is not turned into an AttentionItem", () => {
    const data = makeData({ warnings: [makeWarning({ target_candidate_id: null })] });

    expect(needsAttention(data)).toEqual([]);
  });

  it("rule 3: measurement_text has a numeric+unit hint but measurements[] is empty", () => {
    const data = makeData({ defects: [makeDefect({ measurement_text: "约0.5m左右", measurements: [] })] });

    const items = needsAttention(data);

    expect(items).toContainEqual(
      expect.objectContaining({ kind: "defect", candidateId: "defect_0001" })
    );
  });

  it("rule 3 does not duplicate an existing measurement parse warning", () => {
    const data = makeData({
      defects: [
        makeDefect({
          measurement_text: "约20m左右",
          measurements: [],
          warnings: [
            makeWarning({
              code: "measurement_parse_low_confidence",
              message: "尺寸表达未能稳定结构化，请人工确认。",
            }),
          ],
        }),
      ],
    });

    const items = needsAttention(data).filter(
      (item) => item.warningCode === "measurement_parse_low_confidence",
    );
    expect(items).toHaveLength(1);
  });

  it("rule 3 negative: measurement_text has a hint but measurements[] was already structured", () => {
    const data = makeData({
      defects: [
        makeDefect({
          measurement_text: "L=0.8m",
          measurements: [{ dimension_type: "长度", value_type: "single", value: 0.8, minimum_value: null, maximum_value: null, unit: "m", is_approximate: false, source_text: "L=0.8m" }],
        }),
      ],
    });

    expect(needsAttention(data)).toEqual([]);
  });

  it("rule 4: a defect references a photo_number with no photo candidate linked back to it", () => {
    const data = makeData({ defects: [makeDefect({ photo_numbers: ["7"] })], photos: [] });

    const items = needsAttention(data);

    expect(items).toContainEqual(
      expect.objectContaining({ kind: "defect", candidateId: "defect_0001", message: "照片编号 7 未匹配到关联图片。" })
    );
  });

  it("does not repeat a missing-photo warning after the user acknowledges that number", () => {
    const data = makeData({
      defects: [makeDefect({ photo_numbers: ["7"], confirmed_missing_photo_numbers: ["7"] })],
      photos: [],
    });

    expect(needsAttention(data).some((item) => item.message.includes("照片编号 7 未匹配"))).toBe(false);
  });

  it("rule 4 negative: every referenced photo_number has a photo candidate linked back to the defect", () => {
    const data = makeData({
      defects: [makeDefect({ photo_numbers: ["1"] })],
      photos: [makePhoto({ photo_number: "1", linked_defect_candidate_id: "defect_0001" })],
    });

    expect(needsAttention(data)).toEqual([]);
  });

  it("rule 5: a photo with match_status=未关联 is flagged", () => {
    const data = makeData({ photos: [makePhoto({ match_status: "未关联" })] });

    const items = needsAttention(data);

    expect(items).toContainEqual(expect.objectContaining({ kind: "photo", candidateId: "photo_0001" }));
  });

  it("rule 5 negative: an unlinked photo that has been ignored is not flagged", () => {
    const data = makeData({
      photos: [makePhoto({ match_status: "待校对", linked_defect_candidate_id: null, review_status: "已忽略" })],
    });

    expect(needsAttention(data)).toEqual([]);
  });

  it("does not flag an unlinked photo after the user confirms it is unrelated", () => {
    const data = makeData({
      photos: [makePhoto({ match_status: "未关联", linked_defect_candidate_id: null, review_status: "已确认" })],
    });

    expect(needsAttention(data)).toEqual([]);
  });

  it("flags a pending defect group", () => {
    const data = makeData({ defects: [makeDefect({ group_review_status: "待确认" })], photos: [] });

    expect(needsAttention(data)).toContainEqual(
      expect.objectContaining({ kind: "defect", candidateId: "defect_0001", message: "病害及照片尚未完成联合确认。" })
    );
  });

  it("flags a linked photo whose archive path is missing", () => {
    const data = makeData({
      defects: [makeDefect({ photo_numbers: ["1"], group_review_status: "已确认" })],
      photos: [makePhoto({
        photo_number: "1",
        linked_defect_candidate_id: "defect_0001",
        match_status: "已确认",
        review_status: "已确认",
        extracted_file: { temporary_file_name: "tmp.jpg", archive_relative_path: null },
      })],
    });

    expect(needsAttention(data)).toContainEqual(
      expect.objectContaining({ kind: "photo", candidateId: "photo_0001", severity: "error" })
    );
  });

  // 隔离 rule 5 的第二个析取项（linkedEmpty && review_status !== 已忽略），它独立于 match_status===未关联：
  // 一张已抽取但未匹配、且未被忽略的照片（待校对 + 未关联病害 + 待确认）应当进入需要处理。
  // 若删掉这个析取项，仅靠 match_status===未关联 的规则不会覆盖本用例，本测试就会失败。
  it("rule 5 (second disjunct): a 待校对 photo that is unlinked and not ignored is flagged", () => {
    const data = makeData({
      photos: [makePhoto({ match_status: "待校对", linked_defect_candidate_id: null, review_status: "待确认" })],
    });

    const items = needsAttention(data);

    expect(items).toContainEqual(expect.objectContaining({ kind: "photo", candidateId: "photo_0001" }));
  });

  it("rule 2 (rating target): a top-level warning targeting ratings.overall is classified as kind 'rating'", () => {
    const data = makeData({
      ratings: { overall: makeOverallRating(), structure_parts: [], evaluation_parts: [], component_ratings: [], warnings: [] },
      warnings: [makeWarning({ message: "全桥评分待人工复核。", target_candidate_id: "ratings.overall" })],
    });

    const items = needsAttention(data);

    expect(items).toContainEqual(
      expect.objectContaining({ kind: "rating", candidateId: "ratings.overall", message: "全桥评分待人工复核。" })
    );
  });

  it("rule 2 (import fallthrough): a targeted warning matching no defect/photo/rating is classified as kind 'import'", () => {
    const data = makeData({
      defects: [makeDefect()],
      warnings: [makeWarning({ message: "无法定位到具体候选。", target_candidate_id: "unknown_9999" })],
    });

    const items = needsAttention(data);

    expect(items).toContainEqual(
      expect.objectContaining({ kind: "import", candidateId: "unknown_9999", message: "无法定位到具体候选。" })
    );
  });
});

describe("isNormalDefect", () => {
  it("returns true for a pending defect with no warnings, no blocking error, and complete required fields", () => {
    const data = makeData({ defects: [makeDefect()] });

    expect(isNormalDefect(data.defects[0], data)).toBe(true);
  });

  it("returns false when review_status is not 待确认", () => {
    const defect = makeDefect({ review_status: "已确认" });
    const data = makeData({ defects: [defect] });

    expect(isNormalDefect(defect, data)).toBe(false);
  });

  it("returns false when the defect has object-level warnings", () => {
    const defect = makeDefect({ warnings: [makeWarning()] });
    const data = makeData({ defects: [defect] });

    expect(isNormalDefect(defect, data)).toBe(false);
  });

  it("returns false when a top-level error targets this defect", () => {
    const defect = makeDefect();
    const data = makeData({ defects: [defect], errors: [makeWarning({ severity: "error", target_candidate_id: "defect_0001" })] });

    expect(isNormalDefect(defect, data)).toBe(false);
  });

  it("returns false when a required field (component_name) is blank", () => {
    const defect = makeDefect({ component_name: "" });
    const data = makeData({ defects: [defect] });

    expect(isNormalDefect(defect, data)).toBe(false);
  });

  it("returns false when a referenced photo_number has no photo candidate linked back to it", () => {
    const defect = makeDefect({ photo_numbers: ["1"] });
    const data = makeData({ defects: [defect], photos: [] });

    expect(isNormalDefect(defect, data)).toBe(false);
  });
});

describe("isNormalPhoto", () => {
  it("returns true for a pending, high-confidence, linked photo with no warnings", () => {
    const photo = makePhoto({ match_status: "高置信候选" });

    expect(isNormalPhoto(photo)).toBe(true);
  });

  it("returns false when review_status is not 待确认", () => {
    expect(isNormalPhoto(makePhoto({ match_status: "高置信候选", review_status: "已确认" }))).toBe(false);
  });

  it("returns false when the photo has object-level warnings", () => {
    expect(isNormalPhoto(makePhoto({ match_status: "高置信候选", warnings: [makeWarning()] }))).toBe(false);
  });

  it("returns false when match_status is not 高置信候选", () => {
    expect(isNormalPhoto(makePhoto({ match_status: "已确认" }))).toBe(false);
  });

  it("returns false when linked_defect_candidate_id is empty", () => {
    expect(isNormalPhoto(makePhoto({ match_status: "高置信候选", linked_defect_candidate_id: null }))).toBe(false);
  });
});

describe("isNormalRating", () => {
  it("overall: returns true when pending with a score and a non-empty grade", () => {
    expect(isNormalRating(makeOverallRating())).toBe(true);
  });

  it("overall: returns false when overall_grade is blank", () => {
    expect(isNormalRating(makeOverallRating({ overall_grade: "" }))).toBe(false);
  });

  it("overall: returns false when review_status is not 待确认", () => {
    expect(isNormalRating(makeOverallRating({ review_status: "已修改" }))).toBe(false);
  });

  it("structure part: returns true when pending with a finite score", () => {
    expect(isNormalRating(makeStructurePartRating())).toBe(true);
  });

  it("structure part: returns false when the score is not a finite number", () => {
    expect(isNormalRating(makeStructurePartRating({ structure_score: NaN }))).toBe(false);
  });

  it("structure part: returns false when review_status is not 待确认", () => {
    expect(isNormalRating(makeStructurePartRating({ review_status: "已忽略" }))).toBe(false);
  });

  it("evaluation part: returns true when pending with a finite score", () => {
    expect(isNormalRating(makeEvaluationPartRating())).toBe(true);
  });

  it("evaluation part: returns false when review_status is not 待确认", () => {
    expect(isNormalRating(makeEvaluationPartRating({ review_status: "已确认" }))).toBe(false);
  });
});

describe("buildStatistics", () => {
  it("returns all-zero buckets for an empty candidate set", () => {
    const data = makeData();

    expect(buildStatistics(data)).toEqual({
      defect_count: 0,
      photo_count: 0,
      rating_item_count: 1, // ratings.overall 总是存在，计 1
      pending_count: 1, // 默认的 overall rating 处于待确认
      confirmed_count: 0,
      modified_count: 0,
      ignored_count: 0,
      object_warning_count: 0,
      needs_attention_count: 0,
    });
  });

  it("excludes the temporary imported-rating projection for native 2.0 data", () => {
    const statistics = buildStatistics(makeData(), false);

    expect(statistics.rating_item_count).toBe(0);
    expect(statistics.pending_count).toBe(0);
    expect(statistics.confirmed_count).toBe(0);
    expect(statistics.modified_count).toBe(0);
    expect(statistics.ignored_count).toBe(0);
  });

  it("tallies defects/photos/ratings across the three status layers plus needs_attention_count", () => {
    const data = makeData({
      defects: [
        makeDefect({ candidate_id: "defect_0001", review_status: "待确认" }),
        makeDefect({ candidate_id: "defect_0002", review_status: "已确认", warnings: [makeWarning()] }),
      ],
      photos: [makePhoto({ candidate_id: "photo_0001", review_status: "已忽略" })],
      ratings: {
        overall: makeOverallRating({ review_status: "已修改" }),
        structure_parts: [makeStructurePartRating({ review_status: "待确认" })],
        evaluation_parts: [makeEvaluationPartRating({ review_status: "已确认" })],
        component_ratings: [],
        warnings: [],
      },
    });

    const stats = buildStatistics(data);

    expect(stats.defect_count).toBe(2);
    expect(stats.photo_count).toBe(1);
    expect(stats.rating_item_count).toBe(3); // overall + 1 structure part + 1 evaluation part
    expect(stats.pending_count).toBe(2); // defect_0001 + structure part
    expect(stats.confirmed_count).toBe(2); // defect_0002 + evaluation part
    expect(stats.modified_count).toBe(1); // overall
    expect(stats.ignored_count).toBe(1); // photo_0001
    expect(stats.object_warning_count).toBe(1); // defect_0002 has a warning
    expect(stats.needs_attention_count).toBe(needsAttention(data).length);
    expect(stats.needs_attention_count).toBeGreaterThan(0);
  });
});

// ---------------------------------------------------------------------------
// 合同 1.2：构件评分的注意力清单、普通判定与统计口径
// ---------------------------------------------------------------------------

function makeComponentRating(overrides: Partial<ComponentRatingCandidate> = {}): ComponentRatingCandidate {
  return {
    candidate_id: "component_rating_0001",
    component_ref: { structure_part: "上部结构", component_name: "主梁", component_alias: "2-1#板" },
    source_score: 55.81,
    calculated_score: 55.80761184457488,
    confirmed_score: 55.81,
    score_validation_status: "一致",
    score_resolution_reason: null,
    deduction_defect_candidate_ids: [],
    calculation_details: null,
    review_status: "待确认",
    warnings: [],
    ...overrides,
  };
}

function ratingsWith(componentRatings: ComponentRatingCandidate[]) {
  return {
    overall: makeOverallRating(),
    structure_parts: [],
    evaluation_parts: [],
    component_ratings: componentRatings,
    warnings: [],
  };
}

describe("component rating grouping", () => {
  it("unresolved score validation states appear in needsAttention as rating items", () => {
    const data = makeData({
      ratings: ratingsWith([
        makeComponentRating({
          score_validation_status: "不一致",
          confirmed_score: null,
        }),
      ]),
    });

    const items = needsAttention(data);

    expect(items).toContainEqual(
      expect.objectContaining({
        kind: "rating",
        candidateId: "component_rating_0001",
        severity: "warning",
      })
    );
  });

  it("consistent and manually resolved ratings do not enter needsAttention", () => {
    const data = makeData({
      ratings: ratingsWith([
        makeComponentRating(),
        makeComponentRating({
          candidate_id: "component_rating_0002",
          score_validation_status: "人工接受Word值",
          confirmed_score: 70,
          score_resolution_reason: "已复核",
          review_status: "已修改",
        }),
      ]),
    });

    const items = needsAttention(data).filter((item) => item.candidateId.startsWith("component_rating_"));
    expect(items).toEqual([]);
  });

  it("component rating warnings enter needsAttention with kind rating", () => {
    const data = makeData({
      ratings: ratingsWith([
        makeComponentRating({
          warnings: [makeWarning({ message: "组内评分不一致。", target_candidate_id: "component_rating_0001" })],
        }),
      ]),
    });

    const items = needsAttention(data);
    expect(items).toContainEqual(
      expect.objectContaining({ kind: "rating", candidateId: "component_rating_0001", message: "组内评分不一致。" })
    );
  });

  it("isNormalComponentRating only accepts pending, warning-free, consistent ratings", () => {
    expect(isNormalComponentRating(makeComponentRating())).toBe(true);
    expect(isNormalComponentRating(makeComponentRating({ review_status: "已确认" }))).toBe(false);
    expect(
      isNormalComponentRating(makeComponentRating({ score_validation_status: "不一致", confirmed_score: null }))
    ).toBe(false);
    expect(isNormalComponentRating(makeComponentRating({ warnings: [makeWarning()] }))).toBe(false);
  });

  it("buildStatistics counts component ratings in totals and status buckets", () => {
    const data = makeData({
      ratings: ratingsWith([
        makeComponentRating(),
        makeComponentRating({ candidate_id: "component_rating_0002", review_status: "已修改" }),
      ]),
    });

    const counts = buildStatistics(data);

    expect(counts.rating_item_count).toBe(3); // overall + 2 component ratings
    expect(counts.pending_count).toBeGreaterThanOrEqual(2); // overall(待确认) + component_rating_0001
    expect(counts.modified_count).toBe(1);
  });
});

describe("component inventory matching attention", () => {
  it("adds an exact component_match navigation target when a defect is unlinked", () => {
    const draft = makeData({
      defects: [makeDefect({ bridge_component_id: null, warnings: [] })],
    });

    expect(needsAttention(draft)).toContainEqual(expect.objectContaining({
      kind: "defect",
      candidateId: "defect_0001",
      warningCode: "defect_component_match_required",
      targetField: "component_match",
    }));
  });
});
