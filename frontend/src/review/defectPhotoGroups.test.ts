import { describe, expect, it } from "vitest";

import type { BridgeAnnualInspectionData, DefectCandidate, PhotoCandidate } from "../contracts/annualInspection";
import { buildDefectPhotoGroup, canConfirmDefectPhotoGroup } from "./defectPhotoGroups";

function defect(overrides: Partial<DefectCandidate> = {}): DefectCandidate {
  return {
    candidate_id: "defect_0001",
    structure_part: "上部结构",
    component_name: "主梁",
    component_number: "2-1#梁",
    bridge_component_id: "component-1",
    standard_component_category_id: "main-girder",
    resolved_structure_part: "上部结构",
    component_alias: null,
    defect_type: "裂缝",
    defect_location: "第二跨",
    defect_description: "跨中横向裂缝",
    quantity_text: "1处",
    measurement_text: "L=0.8m",
    measurements: [],
    photo_numbers: ["2.1-1", "2.1-2"],
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

function photo(overrides: Partial<PhotoCandidate> = {}): PhotoCandidate {
  return {
    candidate_id: "photo_0001",
    photo_number: "2.1-1",
    linked_defect_candidate_id: "defect_0001",
    extracted_file: {
      temporary_file_name: "photo_0001.jpg",
      original_caption: "照片 2.1-1",
      archive_relative_path: "photos/photo_0001.jpg",
    },
    match_status: "已确认",
    source_ref: {},
    confidence: 0.9,
    review_status: "已确认",
    warnings: [],
    ...overrides,
  };
}

function data(overrides: Partial<BridgeAnnualInspectionData> = {}): BridgeAnnualInspectionData {
  return {
    contract: {
      name: "BridgeAnnualInspectionData",
      version: "2.0",
      generated_at: "2026-07-11T00:00:00+08:00",
      producer: "test",
      parser_name: "test",
      parser_version: "1.0.0",
    },
    import_context: {
      source_type: "软件导出Word",
      file_role: "当前年度检测资料",
      archived_file_system_number: "GDWJ-000001",
      import_record_system_number: "DRJL-000001",
    },
    bridge_check: { selected_bridge_system_number: "QL-000001", match_status: "匹配", warnings: [] },
    inspection: {
      inspection_year: 2026,
      inspection_date: "2026-07-01",
      report_number: "TEST-001",
      project_name: "测试项目",
      data_role: "当前年度",
    },
    defects: [defect()],
    photos: [photo()],
    ratings: {
      overall: { total_score: 90, overall_grade: "2类", source_ref: {}, confidence: 0.9, review_status: "已确认" },
      structure_parts: [],
      evaluation_parts: [],
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

describe("defect photo groups", () => {
  it("groups linked photos and reports referenced numbers with no candidate", () => {
    const group = buildDefectPhotoGroup(data(), "defect_0001");

    expect(group?.photos.map((item) => item.candidate_id)).toEqual(["photo_0001"]);
    expect(group?.missingPhotoNumbers).toEqual(["2.1-2"]);
  });

  it("returns null for an unknown defect", () => {
    expect(buildDefectPhotoGroup(data(), "defect_missing")).toBeNull();
  });

  it("blocks confirmation until every truly missing number is acknowledged", () => {
    const result = canConfirmDefectPhotoGroup(data(), "defect_0001");

    expect(result.ok).toBe(false);
    expect(result.reasons).toContain("missing_photo_confirmation_required");
  });

  it("allows confirmation when fields, photos, archives, and missing acknowledgements are complete", () => {
    const complete = data({
      defects: [defect({ confirmed_missing_photo_numbers: ["2.1-2"] })],
    });

    expect(canConfirmDefectPhotoGroup(complete, "defect_0001")).toEqual({ ok: true, reasons: [] });
  });

  it("does not treat an existing but unlinked referenced candidate as a missing photo", () => {
    const unlinked = data({
      photos: [photo({ linked_defect_candidate_id: null, match_status: "待校对", review_status: "待确认" })],
      defects: [defect({ photo_numbers: ["2.1-1"], confirmed_missing_photo_numbers: ["2.1-1"] })],
    });
    const group = buildDefectPhotoGroup(unlinked, "defect_0001");
    const result = canConfirmDefectPhotoGroup(unlinked, "defect_0001");

    expect(group?.missingPhotoNumbers).toEqual([]);
    expect(result.ok).toBe(false);
    expect(result.reasons).toContain("photo_link_unresolved");
  });

  it("does not let missing-photo acknowledgement bypass a broken archive", () => {
    const broken = data({
      defects: [defect({ photo_numbers: ["2.1-1"], confirmed_missing_photo_numbers: ["2.1-1"] })],
      photos: [photo({ extracted_file: { temporary_file_name: "photo_0001.jpg", archive_relative_path: null } })],
    });
    const result = canConfirmDefectPhotoGroup(broken, "defect_0001");

    expect(result.ok).toBe(false);
    expect(result.reasons).toContain("photo_archive_missing");
  });

  it("blocks defects with missing required fields or targeted errors", () => {
    const invalid = data({
      defects: [defect({ component_name: "" })],
      errors: [{ code: "bad_defect", message: "病害错误", severity: "error", target_candidate_id: "defect_0001" }],
    });
    const result = canConfirmDefectPhotoGroup(invalid, "defect_0001");

    expect(result.reasons).toContain("defect_required_field_missing");
    expect(result.reasons).toContain("defect_blocking_error");
  });
});
