import { describe, expect, it } from "vitest";

import type { BridgeAnnualInspectionData } from "./annualInspection";
import { isBridgeAnnualInspectionData } from "./annualInspection";

const validData: BridgeAnnualInspectionData = {
  contract: {
    name: "BridgeAnnualInspectionData",
    version: "3.0",
    generated_at: "2026-07-03T00:00:00+08:00",
    producer: "bridge-report-system",
    parser_name: "annual_inspection_contract_parser",
    parser_version: "2.0.0",
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
      source_structure_part: "上部结构",
      component_name: "主梁",
      component_number: "2-1#梁",
      bridge_component_id: null,
      standard_component_category_id: null,
      resolved_structure_part: null,
      component_inventory_revision_id: null,
      component_match_candidate_ids: [],
      component_match_method: null,
      component_match_confirmed_by: null,
      defect_type: "裂缝",
      defect_location: "第二跨左幅梁底",
      defect_scale: 2,
      defect_description: "梁底发现纵向裂缝，需现场复核。",
      quantity_text: "1处",
      measurement_text: "L=0.8m",
      measurements: [
        {
          dimension_type: "长度",
          value_type: "single",
          value: 0.8,
          minimum_value: null,
          maximum_value: null,
          unit: "m",
          is_approximate: false,
          source_text: "L=0.8m",
        },
      ],
      standard_defect_indicator_id: null,
      photo_references: [
        {
          photo_number: "2.1-1",
          resolution: "pending",
          photo_candidate_id: null,
          resolved_defect_candidate_id: null,
          review_note: null,
        },
      ],
      group_review_status: "待确认",
      severity: "warning",
      remark: null,
      source_ref: {
        source_type: "word",
        chapter: "第二章病害",
        table_index: 1,
        row_index: 1,
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
      source_ref: { source_type: "word", table_index: 1, row_index: 1 },
      confidence: 0.95,
      review_status: "待确认",
      warnings: [],
    },
  ],
  comparison_candidates: [],
  report_text_candidates: [],
  warnings: [],
  errors: [],
};

function cloneValidData(): BridgeAnnualInspectionData {
  return JSON.parse(JSON.stringify(validData)) as BridgeAnnualInspectionData;
}

describe("isBridgeAnnualInspectionData 3.0", () => {
  it("accepts valid version three data without imported ratings", () => {
    expect(isBridgeAnnualInspectionData(validData)).toBe(true);
    expect("ratings" in validData).toBe(false);
    expect("defect_deduction" in validData.defects[0]).toBe(false);
  });

  it.each(["1.0", "1.1", "1.2", "2", "2.0", "2.1", "3"])(
    "rejects contract version %s",
    (version) => {
      expect(
        isBridgeAnnualInspectionData({
          ...validData,
          contract: { ...validData.contract, version },
        }),
      ).toBe(false);
    },
  );

  it("rejects imported ratings", () => {
    expect(
      isBridgeAnnualInspectionData({
        ...validData,
        ratings: { overall: { total_score: 85.61 } },
      }),
    ).toBe(false);
  });

  it.each(["defect_deduction", "structure_part", "component_alias"])(
    "rejects legacy defect field %s",
    (fieldName) => {
      expect(
        isBridgeAnnualInspectionData({
          ...validData,
          defects: [{ ...validData.defects[0], [fieldName]: "legacy" }],
        }),
      ).toBe(false);
    },
  );

  it.each([
    ["defect_scale", 0],
    ["defect_scale", -1],
    ["defect_scale", 2.5],
    ["defect_scale", "2"],
  ])("rejects invalid defect field %s = %s", (fieldName, value) => {
    expect(
      isBridgeAnnualInspectionData({
        ...validData,
        defects: [{ ...validData.defects[0], [fieldName]: value }],
      }),
    ).toBe(false);
  });

  it("accepts null scale and null database association fields", () => {
    expect(
      isBridgeAnnualInspectionData({
        ...validData,
        defects: [
          {
            ...validData.defects[0],
            defect_scale: null,
            bridge_component_id: null,
            standard_component_category_id: null,
            resolved_structure_part: null,
          },
        ],
      }),
    ).toBe(true);
  });

  it("accepts audited component matching fields", () => {
    const data = cloneValidData();
    Object.assign(data.defects[0], {
      bridge_component_id: "component-1",
      standard_component_category_id: "category-1",
      resolved_structure_part: "上部结构",
      component_inventory_revision_id: "revision-1",
      component_match_candidate_ids: ["component-1"],
      component_match_method: "manual",
      component_match_confirmed_by: "editor",
    });
    expect(isBridgeAnnualInspectionData(data)).toBe(true);
  });

  it("validates optional range split provenance", () => {
    const data = cloneValidData();
    Object.assign(data.defects[0], {
      range_split_origin: {
        operation_id: "operation-1",
        source_candidate_id: "defect_0001",
        source_component_number: "1-1#板~1-25#板",
        expanded_component_number: "1-7#板",
        split_index: 7,
        split_count: 25,
        operated_by_user_id: "user-1",
        operated_at: "2026-07-24T16:00:00+08:00",
      },
    });
    expect(isBridgeAnnualInspectionData(data)).toBe(true);

    data.defects[0].range_split_origin!.split_index = 26;
    expect(isBridgeAnnualInspectionData(data)).toBe(false);
  });

  it("rejects unknown range split provenance fields", () => {
    const data = cloneValidData();
    Object.assign(data.defects[0], {
      range_split_origin: {
        operation_id: "operation-1",
        source_candidate_id: "defect_0001",
        source_component_number: "1-1#板~1-25#板",
        expanded_component_number: "1-7#板",
        split_index: 7,
        split_count: 25,
        operated_by_user_id: "user-1",
        operated_at: "2026-07-24T16:00:00+08:00",
        unexpected: true,
      },
    });
    expect(isBridgeAnnualInspectionData(data)).toBe(false);
  });

  it("rejects duplicate candidates and unknown component match methods", () => {
    const duplicate = cloneValidData();
    duplicate.defects[0].component_match_candidate_ids = ["component-1", "component-1"];
    expect(isBridgeAnnualInspectionData(duplicate)).toBe(false);

    const unknown = cloneValidData();
    Object.assign(unknown.defects[0], { component_match_method: "fuzzy" });
    expect(isBridgeAnnualInspectionData(unknown)).toBe(false);
  });

  it("accepts a manual source without Word coordinates", () => {
    expect(
      isBridgeAnnualInspectionData({
        ...validData,
        defects: [
          {
            ...validData.defects[0],
            source_ref: { source_type: "manual" },
          },
        ],
      }),
    ).toBe(true);
  });

  it("rejects an unknown source type", () => {
    expect(
      isBridgeAnnualInspectionData({
        ...validData,
        defects: [
          {
            ...validData.defects[0],
            source_ref: { source_type: "spreadsheet" },
          },
        ],
      }),
    ).toBe(false);
  });

  it("rejects duplicate photo reference numbers", () => {
    const invalid = cloneValidData();
    invalid.defects[0].photo_references.push({ ...invalid.defects[0].photo_references[0] });

    expect(isBridgeAnnualInspectionData(invalid)).toBe(false);
  });

  it.each([
    {
      dimension_type: "长度",
      value_type: "range",
      value: null,
      minimum_value: 4,
      maximum_value: 0.5,
      unit: "m",
      is_approximate: false,
      source_text: "4~0.5m",
    },
    {
      dimension_type: "长度",
      value_type: "single",
      value: 1,
      minimum_value: 0.5,
      maximum_value: null,
      unit: "m",
      is_approximate: false,
      source_text: "1m",
    },
  ])("rejects invalid single/range measurement invariants", (measurement) => {
    const invalid = cloneValidData() as unknown as {
      defects: Array<{ measurements: unknown[] }>;
    };
    invalid.defects[0].measurements = [measurement];

    expect(isBridgeAnnualInspectionData(invalid)).toBe(false);
  });

  it("rejects missing top-level and nested required arrays", () => {
    const missingTopLevel = cloneValidData() as unknown as Record<string, unknown>;
    delete missingTopLevel.comparison_candidates;
    expect(isBridgeAnnualInspectionData(missingTopLevel)).toBe(false);

    const missingNested = cloneValidData() as unknown as {
      defects: Array<Record<string, unknown>>;
    };
    delete missingNested.defects[0].measurements;
    expect(isBridgeAnnualInspectionData(missingNested)).toBe(false);
  });

  it("accepts comparison candidates with match basis", () => {
    const data = cloneValidData();
    data.comparison_candidates = [
      {
        candidate_id: "comparison_0001",
        previous_defect_observation_system_number: "BHGC-000123",
        current_defect_observation_system_number: "BHGC-000456",
        comparison_type: "原病害发展",
        match_basis: {
          same_component: true,
          same_defect_type: true,
          location_similarity: 0.82,
          measurement_change_detected: true,
          photo_number_related: false,
        },
        change_summary: "裂缝宽度增加。",
        confidence: 0.86,
        confirmation_status: "待确认",
        review_note: null,
        warnings: [],
      },
    ];

    expect(isBridgeAnnualInspectionData(data)).toBe(true);
  });
});
