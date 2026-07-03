import { describe, expect, it } from "vitest";

import type { BridgeAnnualInspectionData } from "./annualInspection";
import { isBridgeAnnualInspectionData } from "./annualInspection";

const validData: BridgeAnnualInspectionData = {
  contract: {
    name: "BridgeAnnualInspectionData",
    version: "1.0",
    generated_at: "2026-07-03T00:00:00+08:00",
    producer: "bridge-report-system",
    parser_name: "annual_inspection_contract_parser",
    parser_version: "1.0.0",
  },
  import_context: {
    source_type: "软件导出Word",
    file_role: "当前年度检测资料",
    archived_file_system_number: "ARCH-2026-0001",
    import_record_system_number: "IMP-2026-0001",
  },
  bridge_check: {
    selected_bridge_system_number: "QL-000001",
    extracted_bridge_name: "绕阳河二号桥",
    match_status: "匹配",
    warnings: [],
  },
  inspection: {
    inspection_year: 2026,
    inspection_date: "2026-05-18",
    report_number: "BG-2026-RYH02-001",
    project_name: "绕阳河二号桥2026年度检查",
    data_role: "当前年度",
  },
  defects: [
    {
      candidate_id: "defect_0001",
      structure_part: "上部结构",
      component_name: "主梁",
      component_alias: "上部承重构件",
      defect_type: "裂缝",
      defect_location: "第二跨左幅梁底",
      defect_description: "梁底发现纵向裂缝，需现场复核。",
      quantity_text: "1处",
      measurement_text: "L=0.8m，W=0.12mm",
      measurements: [
        {
          dimension_type: "长度",
          value: 0.8,
          unit: "m",
          source_text: "L=0.8m",
        },
      ],
      photo_numbers: ["2.1-1"],
      severity: "warning",
      remark: null,
      source_ref: {
        chapter: "第二章病害",
        table_title: "病害记录表",
        table_index: 1,
        row_index: 1,
        column_name: "病害描述",
        raw_row_text: "第二跨左幅梁底主梁裂缝，L=0.8m，W=0.12mm。",
        photo_area_caption: null,
        file_role: "当前年度检测资料",
        paragraph_index: null,
      },
      confidence: 0.92,
      review_status: "待确认",
      review_note: null,
      warnings: [],
    },
  ],
  photos: [
    {
      candidate_id: "photo_0001",
      photo_number: "2.1-1",
      linked_defect_candidate_id: "defect_0001",
      extracted_file: {
        temporary_file_name: "photo_0001.jpg",
        original_caption: "主梁梁底裂缝",
        archive_relative_path: "photos/2.1-1.jpg",
      },
      match_status: "高置信候选",
      source_ref: {
        chapter: "第二章病害",
        table_title: "照片记录表",
        table_index: 1,
        row_index: 1,
        column_name: "照片",
        raw_row_text: "照片2.1-1 主梁梁底裂缝。",
        photo_area_caption: "主梁梁底裂缝",
        file_role: "当前年度检测资料",
        paragraph_index: null,
      },
      confidence: 0.95,
      review_status: "待确认",
      warnings: [],
    },
  ],
  ratings: {
    overall: {
      total_score: 85.61,
      overall_grade: "2类",
      source_ref: {
        chapter: "第四章总体技术状况评定表",
        table_title: "总体技术状况评定表",
        table_index: 1,
        row_index: 1,
        column_name: "总体评分",
        raw_row_text: "总体技术状况评分85.61，等级2类。",
        photo_area_caption: null,
        file_role: "当前年度检测资料",
        paragraph_index: null,
      },
      confidence: 0.95,
      review_status: "待确认",
    },
    structure_parts: [
      {
        structure_part: "上部结构",
        structure_score: 87.45,
        weight: 0.4,
        grade: "2",
        source_ref: {
          chapter: "第四章总体技术状况评定表",
          table_title: "结构部位评分表",
          table_index: 2,
          row_index: 1,
          column_name: "上部结构",
          raw_row_text: "上部结构得分87.45，权重0.4，等级2。",
          photo_area_caption: null,
          file_role: "当前年度检测资料",
          paragraph_index: null,
        },
        confidence: 0.94,
        review_status: "待确认",
      },
    ],
    evaluation_parts: [
      {
        structure_part: "上部结构",
        category_no: 1,
        evaluation_part: "上部承重构件",
        part_score: 86.62,
        score_rows: [
          {
            component_count: 3,
            component_score: 86.62,
          },
        ],
        source_ref: {
          chapter: "第四章总体技术状况评定表",
          table_title: "评定部件评分表",
          table_index: 3,
          row_index: 1,
          column_name: "上部承重构件",
          raw_row_text: "上部承重构件评分86.62。",
          photo_area_caption: null,
          file_role: "当前年度检测资料",
          paragraph_index: null,
        },
        confidence: 0.93,
        review_status: "待确认",
      },
    ],
    warnings: [],
  },
  comparison_candidates: [],
  report_text_candidates: [],
  warnings: [],
  errors: [],
};

function cloneValidData(): BridgeAnnualInspectionData {
  return JSON.parse(JSON.stringify(validData)) as BridgeAnnualInspectionData;
}

describe("isBridgeAnnualInspectionData", () => {
  it("accepts valid annual inspection data", () => {
    expect(isBridgeAnnualInspectionData(validData)).toBe(true);
  });

  it("rejects an evaluation part with grade", () => {
    const invalid = cloneValidData() as unknown as {
      ratings: { evaluation_parts: Array<Record<string, unknown>> };
    };
    invalid.ratings.evaluation_parts[0].grade = "2";

    expect(isBridgeAnnualInspectionData(invalid)).toBe(false);
  });

  it("rejects confidence outside zero to one", () => {
    const invalid = cloneValidData();
    invalid.defects[0].confidence = 1.01;

    expect(isBridgeAnnualInspectionData(invalid)).toBe(false);
  });

  it("rejects missing top-level comparison candidates", () => {
    const invalid = cloneValidData() as unknown as Record<string, unknown>;
    delete invalid.comparison_candidates;

    expect(isBridgeAnnualInspectionData(invalid)).toBe(false);
  });

  it("rejects missing nested defect measurements", () => {
    const invalid = cloneValidData() as unknown as {
      defects: Array<Record<string, unknown>>;
    };
    delete invalid.defects[0].measurements;

    expect(isBridgeAnnualInspectionData(invalid)).toBe(false);
  });

  it("rejects missing rating warnings", () => {
    const invalid = cloneValidData() as unknown as {
      ratings: Record<string, unknown>;
    };
    delete invalid.ratings.warnings;

    expect(isBridgeAnnualInspectionData(invalid)).toBe(false);
  });

  it("accepts comparison candidates with match basis", () => {
    const withComparison = cloneValidData();
    withComparison.comparison_candidates = [
      {
        candidate_id: "comparison_0001",
        previous_defect_observation_system_number: "BHGC-000123",
        current_defect_observation_system_number: "BHGC-000456",
        comparison_type: "原病害发展",
        change_summary: "主梁梁底裂缝宽度由 0.10mm 发展至 0.12mm。",
        match_basis: {
          same_component: true,
          same_defect_type: true,
          location_similarity: 0.82,
          measurement_change_detected: true,
          photo_number_related: false,
        },
        confidence: 0.86,
        confirmation_status: "待确认",
        review_note: null,
        warnings: [],
      },
    ];

    expect(isBridgeAnnualInspectionData(withComparison)).toBe(true);
    if (!isBridgeAnnualInspectionData(withComparison)) {
      throw new Error("Expected annual inspection data");
    }
    expect(withComparison.comparison_candidates[0].match_basis?.location_similarity).toBe(0.82);
  });
});
