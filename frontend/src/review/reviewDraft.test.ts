import { describe, expect, it } from "vitest";

import type {
  BridgeAnnualInspectionData,
  ComponentRatingCandidate,
  DefectCandidate,
  EvaluationPartRating,
  OverallRating,
  PhotoCandidate,
  StructurePartRating,
} from "../contracts/annualInspection";
import { reviewDraftReducer } from "./reviewDraft";

function makeDefect(overrides: Partial<DefectCandidate> = {}): DefectCandidate {
  return {
    candidate_id: "defect_0001",
    structure_part: "上部结构",
    component_name: "主梁",
    component_alias: null,
    defect_type: "裂缝",
    defect_location: "跨中",
    defect_description: "横向裂缝",
    quantity_text: null,
    measurement_text: null,
    measurements: [],
    photo_numbers: [],
    group_review_status: "待确认",
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
    extracted_file: { temporary_file_name: "tmp_0001.jpg", original_caption: null, archive_relative_path: null },
    match_status: "高置信候选",
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

function makeState(overrides: Partial<BridgeAnnualInspectionData> = {}): BridgeAnnualInspectionData {
  return {
    contract: {
      name: "BridgeAnnualInspectionData",
      version: "1.2",
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
    defects: [makeDefect()],
    photos: [makePhoto()],
    ratings: {
      overall: makeOverallRating(),
      structure_parts: [makeStructurePartRating()],
      evaluation_parts: [makeEvaluationPartRating()],
      component_ratings: [],
      warnings: [],
    },
    comparison_candidates: [],
    report_text_candidates: [],
    warnings: [],
    errors: [],
    ...overrides,
  };
}

describe("reviewDraftReducer", () => {
  it("edit_defect_field updates a whitelisted content field and auto-flips 已确认 -> 已修改", () => {
    const state = makeState({ defects: [makeDefect({ review_status: "已确认" })] });

    const next = reviewDraftReducer(state, {
      type: "edit_defect_field",
      candidateId: "defect_0001",
      field: "component_name",
      value: "边梁",
    });

    expect(next.defects[0].component_name).toBe("边梁");
    expect(next.defects[0].review_status).toBe("已修改");
  });

  it("edit_defect_field on the review_status field behaves like set_defect_status (no auto-flip override)", () => {
    const state = makeState({ defects: [makeDefect({ review_status: "待确认" })] });

    const next = reviewDraftReducer(state, {
      type: "edit_defect_field",
      candidateId: "defect_0001",
      field: "review_status",
      value: "已忽略",
    });

    expect(next.defects[0].review_status).toBe("已忽略");
  });

  it("edit_defect_field content edit does not resurrect an 已忽略 defect", () => {
    const state = makeState({ defects: [makeDefect({ review_status: "已忽略" })] });

    const next = reviewDraftReducer(state, {
      type: "edit_defect_field",
      candidateId: "defect_0001",
      field: "defect_location",
      value: "边跨",
    });

    expect(next.defects[0].defect_location).toBe("边跨");
    expect(next.defects[0].review_status).toBe("已忽略");
  });

  it("edit_measurement_text recomputes measurements[] via parseMeasurements and auto-flips to 已修改", () => {
    const state = makeState({ defects: [makeDefect({ review_status: "待确认", measurement_text: null, measurements: [] })] });

    const next = reviewDraftReducer(state, {
      type: "edit_measurement_text",
      candidateId: "defect_0001",
      text: "L=1.2m，W=0.15m",
    });

    expect(next.defects[0].measurement_text).toBe("L=1.2m，W=0.15m");
    expect(next.defects[0].measurements).toEqual([
      { dimension_type: "长度", value: 1.2, unit: "m", source_text: "L=1.2m" },
      { dimension_type: "宽度", value: 0.15, unit: "m", source_text: "W=0.15m" },
    ]);
    expect(next.defects[0].review_status).toBe("已修改");
  });

  it("set_defect_status sets the exact status given, independent of the auto-flip rule", () => {
    const state = makeState({ defects: [makeDefect({ review_status: "待确认" })] });

    const next = reviewDraftReducer(state, { type: "set_defect_status", candidateId: "defect_0001", status: "已确认" });

    expect(next.defects[0].review_status).toBe("已确认");
  });

  it("photo_confirm_match resolves the photo and invalidates its confirmed group", () => {
    const state = makeState({
      defects: [makeDefect({ group_review_status: "已确认" })],
      photos: [makePhoto({ linked_defect_candidate_id: "defect_0001", match_status: "高置信候选" })],
    });

    const next = reviewDraftReducer(state, { type: "photo_confirm_match", candidateId: "photo_0001" });

    expect(next.photos[0].match_status).toBe("已确认");
    expect(next.photos[0].review_status).toBe("已确认");
    expect(next.defects[0].group_review_status).toBe("待确认");
  });

  it("photo_confirm_match is a no-op when linked_defect_candidate_id is empty", () => {
    const state = makeState({ photos: [makePhoto({ linked_defect_candidate_id: null, match_status: "待校对" })] });

    const next = reviewDraftReducer(state, { type: "photo_confirm_match", candidateId: "photo_0001" });

    expect(next.photos[0].match_status).toBe("待校对");
  });

  it("photo_reset returns the photo to pending and invalidates its group", () => {
    const state = makeState({
      defects: [makeDefect({ group_review_status: "已确认" })],
      photos: [makePhoto({ linked_defect_candidate_id: "defect_0001", match_status: "已确认", review_status: "已确认" })],
    });

    const next = reviewDraftReducer(state, { type: "photo_reset", candidateId: "photo_0001" });

    expect(next.photos[0].match_status).toBe("待校对");
    expect(next.photos[0].review_status).toBe("待确认");
    expect(next.defects[0].group_review_status).toBe("待确认");
  });

  it("photo_mark_unrelated clears the link and records a resolved unrelated decision", () => {
    const state = makeState({
      defects: [makeDefect({ group_review_status: "已确认" })],
      photos: [makePhoto({ linked_defect_candidate_id: "defect_0001" })],
    });

    const next = reviewDraftReducer(state, { type: "photo_mark_unrelated", candidateId: "photo_0001", note: "与病害无关" });

    expect(next.photos[0].linked_defect_candidate_id).toBeNull();
    expect(next.photos[0].match_status).toBe("未关联");
    expect(next.photos[0].review_status).toBe("已确认");
    expect(next.defects[0].group_review_status).toBe("待确认");
  });

  it("photo_ignore clears the link, ignores the photo, and invalidates its group", () => {
    const state = makeState({
      defects: [makeDefect({ group_review_status: "已确认" })],
      photos: [makePhoto({ review_status: "待确认" })],
    });

    const next = reviewDraftReducer(state, { type: "photo_ignore", candidateId: "photo_0001" });

    expect(next.photos[0].linked_defect_candidate_id).toBeNull();
    expect(next.photos[0].review_status).toBe("已忽略");
    expect(next.defects[0].group_review_status).toBe("待确认");
  });

  it("edit_photo_number updates the photo_number field", () => {
    const state = makeState();

    const next = reviewDraftReducer(state, { type: "edit_photo_number", candidateId: "photo_0001", photoNumber: "12" });

    expect(next.photos[0].photo_number).toBe("12");
  });

  it("photo_relink resolves the photo and invalidates both old and new groups", () => {
    const state = makeState({
      defects: [
        makeDefect({ candidate_id: "defect_0001", group_review_status: "已确认" }),
        makeDefect({ candidate_id: "defect_0002", group_review_status: "已确认" }),
      ],
      photos: [makePhoto({ linked_defect_candidate_id: "defect_0001" })],
    });

    const next = reviewDraftReducer(state, { type: "photo_relink", candidateId: "photo_0001", defectCandidateId: "defect_0002" });

    expect(next.photos[0].linked_defect_candidate_id).toBe("defect_0002");
    expect(next.photos[0].match_status).toBe("已确认");
    expect(next.photos[0].review_status).toBe("已修改");
    expect(next.defects.map((item) => item.group_review_status)).toEqual(["待确认", "待确认"]);
  });

  it("confirms and unconfirms a truly missing photo number without duplicates", () => {
    const state = makeState({ defects: [makeDefect({ photo_numbers: ["2.1-9"] })], photos: [] });
    const confirmed = reviewDraftReducer(state, {
      type: "confirm_missing_photo",
      defectCandidateId: "defect_0001",
      photoNumber: "2.1-9",
    });
    const duplicate = reviewDraftReducer(confirmed, {
      type: "confirm_missing_photo",
      defectCandidateId: "defect_0001",
      photoNumber: "2.1-9",
    });
    const unconfirmed = reviewDraftReducer(duplicate, {
      type: "unconfirm_missing_photo",
      defectCandidateId: "defect_0001",
      photoNumber: "2.1-9",
    });

    expect(confirmed.defects[0].confirmed_missing_photo_numbers).toEqual(["2.1-9"]);
    expect(duplicate.defects[0].confirmed_missing_photo_numbers).toEqual(["2.1-9"]);
    expect(unconfirmed.defects[0].confirmed_missing_photo_numbers).toEqual([]);
  });

  it("does not confirm missing when a candidate with that number exists", () => {
    const state = makeState({
      defects: [makeDefect({ photo_numbers: ["1"] })],
      photos: [makePhoto({ photo_number: "1", linked_defect_candidate_id: null })],
    });

    const next = reviewDraftReducer(state, {
      type: "confirm_missing_photo",
      defectCandidateId: "defect_0001",
      photoNumber: "1",
    });

    expect(next.defects[0].confirmed_missing_photo_numbers).toEqual([]);
  });

  it("confirm_defect_group confirms a complete group and preserves 已修改", () => {
    const state = makeState({
      defects: [makeDefect({ review_status: "已修改", photo_numbers: ["1"] })],
      photos: [makePhoto({ match_status: "已确认", review_status: "已确认", extracted_file: {
        temporary_file_name: "tmp_0001.jpg",
        archive_relative_path: "photos/tmp_0001.jpg",
      } })],
    });

    const next = reviewDraftReducer(state, { type: "confirm_defect_group", defectCandidateId: "defect_0001" });

    expect(next.defects[0].group_review_status).toBe("已确认");
    expect(next.defects[0].review_status).toBe("已修改");
  });

  it("editing photo numbers intersects missing acknowledgements and invalidates the group", () => {
    const state = makeState({
      defects: [makeDefect({
        photo_numbers: ["1", "2"],
        confirmed_missing_photo_numbers: ["1", "2"],
        group_review_status: "已确认",
      })],
    });

    const next = reviewDraftReducer(state, {
      type: "edit_defect_field",
      candidateId: "defect_0001",
      field: "photo_numbers",
      value: ["2", "3"],
    });

    expect(next.defects[0].confirmed_missing_photo_numbers).toEqual(["2"]);
    expect(next.defects[0].group_review_status).toBe("待确认");
  });

  it("edit_rating_field on overall sets total_score from a number value (UI coerces strings before dispatch)", () => {
    const state = makeState();

    const next = reviewDraftReducer(state, { type: "edit_rating_field", target: "overall", field: "total_score", value: 88 });

    expect(next.ratings.overall.total_score).toBe(88);
    expect(typeof next.ratings.overall.total_score).toBe("number");
  });

  it("edit_rating_field on a structure part matches by structure_part and updates structure_score", () => {
    const state = makeState({
      ratings: {
        overall: makeOverallRating(),
        structure_parts: [
          makeStructurePartRating({ structure_part: "上部结构", structure_score: 90 }),
          makeStructurePartRating({ structure_part: "下部结构", structure_score: 85 }),
        ],
        evaluation_parts: [],
        component_ratings: [],
        warnings: [],
      },
    });

    const next = reviewDraftReducer(state, {
      type: "edit_rating_field",
      target: { part: "下部结构" },
      field: "structure_score",
      value: 80,
    });

    expect(next.ratings.structure_parts[0].structure_score).toBe(90);
    expect(next.ratings.structure_parts[1].structure_score).toBe(80);
  });

  it("edit_rating_field on an evaluation part matches by index and updates part_score", () => {
    const state = makeState({
      ratings: {
        overall: makeOverallRating(),
        structure_parts: [],
        evaluation_parts: [makeEvaluationPartRating({ part_score: 95 }), makeEvaluationPartRating({ part_score: 88 })],
        component_ratings: [],
        warnings: [],
      },
    });

    const next = reviewDraftReducer(state, { type: "edit_rating_field", target: { evaluation: 1 }, field: "part_score", value: 70 });

    expect(next.ratings.evaluation_parts[0].part_score).toBe(95);
    expect(next.ratings.evaluation_parts[1].part_score).toBe(70);
  });

  it("set_rating_status updates review_status for overall, a structure part, and an evaluation part target", () => {
    const state = makeState({
      ratings: {
        overall: makeOverallRating({ review_status: "待确认" }),
        structure_parts: [makeStructurePartRating({ structure_part: "桥面系", review_status: "待确认" })],
        evaluation_parts: [makeEvaluationPartRating({ review_status: "待确认" })],
        component_ratings: [],
        warnings: [],
      },
    });

    const afterOverall = reviewDraftReducer(state, { type: "set_rating_status", target: "overall", status: "已确认" });
    const afterPart = reviewDraftReducer(state, { type: "set_rating_status", target: { part: "桥面系" }, status: "已确认" });
    const afterEvaluation = reviewDraftReducer(state, { type: "set_rating_status", target: { evaluation: 0 }, status: "已确认" });

    expect(afterOverall.ratings.overall.review_status).toBe("已确认");
    expect(afterPart.ratings.structure_parts[0].review_status).toBe("已确认");
    expect(afterEvaluation.ratings.evaluation_parts[0].review_status).toBe("已确认");
  });

  it("batch_confirm_normal_ratings confirms only normal ratings and leaves defects and photos unchanged", () => {
    const state = makeState({
      defects: [
        makeDefect({ candidate_id: "defect_normal", review_status: "待确认" }),
        makeDefect({ candidate_id: "defect_warned", review_status: "待确认", warnings: [{ code: "x", message: "需要确认", severity: "warning", target_candidate_id: null }] }),
      ],
      photos: [
        makePhoto({ candidate_id: "photo_normal", review_status: "待确认", match_status: "高置信候选", linked_defect_candidate_id: "defect_normal" }),
        makePhoto({ candidate_id: "photo_low_confidence", review_status: "待确认", match_status: "待校对", linked_defect_candidate_id: "defect_normal" }),
      ],
      ratings: {
        overall: makeOverallRating({ review_status: "待确认" }),
        structure_parts: [],
        evaluation_parts: [],
        component_ratings: [],
        warnings: [],
      },
    });

    const next = reviewDraftReducer(state, { type: "batch_confirm_normal_ratings" });

    expect(next.defects.find((d) => d.candidate_id === "defect_normal")?.review_status).toBe("待确认");
    expect(next.defects.find((d) => d.candidate_id === "defect_warned")?.review_status).toBe("待确认");
    expect(next.photos.find((p) => p.candidate_id === "photo_normal")?.review_status).toBe("待确认");
    expect(next.photos.find((p) => p.candidate_id === "photo_low_confidence")?.review_status).toBe("待确认");
    expect(next.ratings.overall.review_status).toBe("已确认");
  });

  it("never mutates the original state passed in (immutability)", () => {
    const state = makeState();
    const snapshot = JSON.parse(JSON.stringify(state)) as BridgeAnnualInspectionData;

    const next = reviewDraftReducer(state, {
      type: "edit_defect_field",
      candidateId: "defect_0001",
      field: "component_name",
      value: "变更后的构件名",
    });

    expect(state).toEqual(snapshot);
    expect(next).not.toBe(state);
    expect(next.defects).not.toBe(state.defects);
    expect(next.defects[0]).not.toBe(state.defects[0]);
    // 未涉及的顶层字段保持引用相等——reducer 只重建被改动的分支，不做无谓深拷贝。
    expect(next.photos).toBe(state.photos);
    expect(next.ratings).toBe(state.ratings);
  });
});

// ---------------------------------------------------------------------------
// 合同 1.2：构件评分联动重算与人工差异处理
// ---------------------------------------------------------------------------

function makeComponentRating(overrides: Partial<ComponentRatingCandidate> = {}): ComponentRatingCandidate {
  return {
    candidate_id: "component_rating_0001",
    component_ref: { structure_part: "上部结构", component_name: "主梁", component_alias: null },
    source_score: 65,
    calculated_score: 65,
    confirmed_score: 65,
    score_validation_status: "一致",
    score_resolution_reason: null,
    deduction_defect_candidate_ids: ["defect_0001"],
    calculation_details: { standard: "JTG/T H21-2011 4.1.1", ordered_deductions: [35], rounding_scale: 2 },
    review_status: "待确认",
    warnings: [],
    ...overrides,
  };
}

function makeScoredState(
  ratingOverrides: Partial<ComponentRatingCandidate> = {},
  defectOverrides: Partial<DefectCandidate> = {}
): BridgeAnnualInspectionData {
  return makeState({
    defects: [makeDefect({ defect_scale: 2, defect_deduction: 35, ...defectOverrides })],
    ratings: {
      overall: makeOverallRating(),
      structure_parts: [],
      evaluation_parts: [],
      component_ratings: [makeComponentRating(ratingOverrides)],
      warnings: [],
    },
  });
}

describe("component rating recomputation", () => {
  it("edit_defect_field on defect_scale and defect_deduction updates the defect and flips status to 已修改", () => {
    const state = makeScoredState();
    const afterScale = reviewDraftReducer(state, {
      type: "edit_defect_field",
      candidateId: "defect_0001",
      field: "defect_scale",
      value: 3,
    });
    expect(afterScale.defects[0].defect_scale).toBe(3);
    expect(afterScale.defects[0].review_status).toBe("已修改");
    expect(afterScale.defects[0].group_review_status).toBe("待确认");

    const afterClear = reviewDraftReducer(afterScale, {
      type: "edit_defect_field",
      candidateId: "defect_0001",
      field: "defect_scale",
      value: null,
    });
    expect(afterClear.defects[0].defect_scale).toBeNull();
  });

  it("changing a deduction recomputes the component rating and reclassifies the status", () => {
    const state = makeScoredState();
    const next = reviewDraftReducer(state, {
      type: "edit_defect_field",
      candidateId: "defect_0001",
      field: "defect_deduction",
      value: 20,
    });

    const rating = next.ratings.component_ratings[0];
    expect(rating.calculated_score).toBe(80);
    expect(rating.calculation_details?.ordered_deductions).toEqual([20]);
    expect(rating.score_validation_status).toBe("不一致");
    expect(rating.confirmed_score).toBeNull();
    expect(rating.score_resolution_reason).toBeNull();
    expect(rating.review_status).toBe("待确认");
    // 原 state 不被原地修改。
    expect(state.ratings.component_ratings[0].calculated_score).toBe(65);
  });

  it("clearing a deduction makes the rating 无法复算 without fabricating deductions", () => {
    const state = makeScoredState();
    const next = reviewDraftReducer(state, {
      type: "edit_defect_field",
      candidateId: "defect_0001",
      field: "defect_deduction",
      value: null,
    });

    const rating = next.ratings.component_ratings[0];
    expect(rating.calculated_score).toBeNull();
    expect(rating.calculation_details).toBeNull();
    expect(rating.score_validation_status).toBe("无法复算");
    expect(rating.confirmed_score).toBeNull();
  });

  it("ignoring a defect shrinks the deduction evidence and recomputes", () => {
    const state = makeState({
      defects: [
        makeDefect({ candidate_id: "defect_0001", defect_deduction: 35 }),
        makeDefect({ candidate_id: "defect_0002", defect_deduction: 20 }),
      ],
      ratings: {
        overall: makeOverallRating(),
        structure_parts: [],
        evaluation_parts: [],
        component_ratings: [
          makeComponentRating({
            source_score: 55.81,
            calculated_score: 55.80761184457488,
            confirmed_score: 55.81,
            deduction_defect_candidate_ids: ["defect_0001", "defect_0002"],
            calculation_details: { standard: "JTG/T H21-2011 4.1.1", ordered_deductions: [35, 20], rounding_scale: 2 },
          }),
        ],
        warnings: [],
      },
    });

    const next = reviewDraftReducer(state, { type: "set_defect_status", candidateId: "defect_0002", status: "已忽略" });

    const rating = next.ratings.component_ratings[0];
    expect(rating.deduction_defect_candidate_ids).toEqual(["defect_0001"]);
    expect(rating.calculated_score).toBe(65);
    expect(rating.score_validation_status).toBe("不一致");
    expect(rating.confirmed_score).toBeNull();
  });

  it("keeps the rating object untouched when an unrelated edit does not change evidence", () => {
    const state = makeScoredState();
    const next = reviewDraftReducer(state, {
      type: "edit_defect_field",
      candidateId: "defect_0001",
      field: "defect_location",
      value: "左侧端部",
    });
    expect(next.ratings.component_ratings[0]).toBe(state.ratings.component_ratings[0]);
  });
});

describe("resolve_component_score", () => {
  it("accept_source stores the source score with reason and flips to 已修改", () => {
    const state = makeScoredState({
      source_score: 70,
      calculated_score: 65,
      confirmed_score: null,
      score_validation_status: "不一致",
    });
    const next = reviewDraftReducer(state, {
      type: "resolve_component_score",
      candidateId: "component_rating_0001",
      choice: "accept_source",
      reason: "现场复核后采信 Word 分值",
    });

    const rating = next.ratings.component_ratings[0];
    expect(rating.confirmed_score).toBe(70);
    expect(rating.score_validation_status).toBe("人工接受Word值");
    expect(rating.score_resolution_reason).toBe("现场复核后采信 Word 分值");
    expect(rating.review_status).toBe("已修改");
  });

  it("adopt_calculated stores the rounded calculated score", () => {
    const state = makeScoredState({
      source_score: 70,
      calculated_score: 55.80761184457488,
      confirmed_score: null,
      score_validation_status: "不一致",
    });
    const next = reviewDraftReducer(state, {
      type: "resolve_component_score",
      candidateId: "component_rating_0001",
      choice: "adopt_calculated",
      reason: "扣分依据完整，采用规范复算值",
    });

    const rating = next.ratings.component_ratings[0];
    expect(rating.confirmed_score).toBe(55.81);
    expect(rating.score_validation_status).toBe("人工采用复算值");
  });

  it("is a no-op without a reason or when the status is already consistent", () => {
    const unresolved = makeScoredState({
      source_score: 70,
      confirmed_score: null,
      score_validation_status: "不一致",
    });
    expect(
      reviewDraftReducer(unresolved, {
        type: "resolve_component_score",
        candidateId: "component_rating_0001",
        choice: "accept_source",
        reason: "   ",
      })
    ).toBe(unresolved);

    const consistent = makeScoredState();
    expect(
      reviewDraftReducer(consistent, {
        type: "resolve_component_score",
        candidateId: "component_rating_0001",
        choice: "accept_source",
        reason: "不需要的原因",
      })
    ).toBe(consistent);
  });

  it("reset_component_score_resolution returns to the automatic classification", () => {
    const state = makeScoredState({
      source_score: 70,
      calculated_score: 65,
      confirmed_score: 70,
      score_validation_status: "人工接受Word值",
      score_resolution_reason: "早前的选择",
      review_status: "已修改",
    });
    const next = reviewDraftReducer(state, {
      type: "reset_component_score_resolution",
      candidateId: "component_rating_0001",
    });

    const rating = next.ratings.component_ratings[0];
    expect(rating.score_validation_status).toBe("不一致");
    expect(rating.confirmed_score).toBeNull();
    expect(rating.score_resolution_reason).toBeNull();
    expect(rating.review_status).toBe("待确认");
  });

  it("batch_confirm_normal_ratings confirms consistent component ratings only", () => {
    const state = makeState({
      defects: [makeDefect({ defect_deduction: 35 })],
      ratings: {
        overall: makeOverallRating({ review_status: "已确认" }),
        structure_parts: [],
        evaluation_parts: [],
        component_ratings: [
          makeComponentRating({ candidate_id: "component_rating_0001" }),
          makeComponentRating({
            candidate_id: "component_rating_0002",
            component_ref: { structure_part: "上部结构", component_name: "横梁", component_alias: null },
            source_score: 70,
            calculated_score: null,
            confirmed_score: null,
            score_validation_status: "无法复算",
            calculation_details: null,
            deduction_defect_candidate_ids: [],
          }),
        ],
        warnings: [],
      },
    });

    const next = reviewDraftReducer(state, { type: "batch_confirm_normal_ratings" });

    expect(next.ratings.component_ratings[0].review_status).toBe("已确认");
    expect(next.ratings.component_ratings[1].review_status).toBe("待确认");
  });
});
