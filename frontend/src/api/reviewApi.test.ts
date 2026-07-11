import { afterEach, describe, expect, it, vi } from "vitest";

import type { BridgeAnnualInspectionData } from "../contracts/annualInspection";
import { ApiError } from "./apiClient";
import { cancelImport, confirmImport, fetchReview, photoContentUrl, runPreflight, saveReviewDraft } from "./reviewApi";

// 满足 isBridgeAnnualInspectionData 最小必填字段集合的候选数据骨架，供测试复用。
const minimalParsedResult: BridgeAnnualInspectionData = {
  contract: {
    name: "BridgeAnnualInspectionData",
    version: "1.1",
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
  bridge_check: {
    selected_bridge_system_number: "QL-000001",
    match_status: "匹配",
    warnings: [],
  },
  inspection: {
    inspection_year: 2026,
    inspection_date: "2026-05-18",
    report_number: "BG-2026-0001",
    project_name: "测试项目",
    data_role: "当前年度",
  },
  defects: [],
  photos: [],
  ratings: {
    overall: {
      total_score: 90,
      overall_grade: "1类",
      source_ref: {},
      confidence: 0.9,
      review_status: "待确认",
    },
    structure_parts: [],
    evaluation_parts: [],
    warnings: [],
  },
  comparison_candidates: [],
  report_text_candidates: [],
  warnings: [],
  errors: [],
};

function reviewResponseBody(overrides: Partial<Record<string, unknown>> = {}) {
  return {
    import_record: {
      id: "record-1",
      system_number: "IMP-000001",
      import_name: "2026年度检查报告.docx",
      source_type: "软件导出Word",
      import_status: "待校对",
      importer_name: "张三",
      importer_version: null,
      created_at: "2026-07-01T00:00:00+08:00",
      updated_at: "2026-07-01T00:00:00+08:00",
    },
    bridge: {
      id: "bridge-1",
      system_number: "QL-000001",
      bridge_name: "绕阳河二号桥",
      route_name: "S101",
    },
    inspection_year: null,
    parsed_result: minimalParsedResult,
    statistics: {
      defect_count: 0,
      photo_count: 0,
      rating_item_count: 0,
      pending_count: 0,
      confirmed_count: 0,
      modified_count: 0,
      ignored_count: 0,
      object_warning_count: 0,
    },
    has_current_annual_facts: false,
    contract_compatibility: "native_1_1",
    ...overrides,
  };
}

describe("reviewApi", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("builds an encoded photo content URL", () => {
    expect(photoContentUrl("http://127.0.0.1:18080/", "a/b", "p 1"))
      .toBe("http://127.0.0.1:18080/api/import-records/a%2Fb/photos/p%201/content");
  });

  it("fetchReview requests GET .../review and returns the parsed body when parsed_result is valid", async () => {
    const body = reviewResponseBody();
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => body,
    });
    vi.stubGlobal("fetch", fetchMock);

    const review = await fetchReview("http://127.0.0.1:18080", "record-1");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/import-records/record-1/review");
    expect(review.parsed_result).toEqual(minimalParsedResult);
    expect(review.statistics.defect_count).toBe(0);
    expect(review.contract_compatibility).toBe("native_1_1");
  });

  it("fetchReview accepts an upgraded_1_0 response with normalized 1.1 data", async () => {
    const body = reviewResponseBody({ contract_compatibility: "upgraded_1_0" });
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => body,
    }));

    const review = await fetchReview("http://127.0.0.1:18080", "record-1");

    expect(review.contract_compatibility).toBe("upgraded_1_0");
    expect(review.parsed_result.contract.version).toBe("1.1");
  });

  it("fetchReview accepts a legacy_read_only response with normalized 1.1 data", async () => {
    const body = reviewResponseBody({
      import_record: {
        ...reviewResponseBody().import_record,
        import_status: "已确认",
      },
      contract_compatibility: "legacy_read_only",
    });
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => body,
    }));

    const review = await fetchReview("http://127.0.0.1:18080", "record-1");

    expect(review.import_record.import_status).toBe("已确认");
    expect(review.contract_compatibility).toBe("legacy_read_only");
    expect(review.parsed_result.contract.version).toBe("1.1");
  });

  it("fetchReview throws ApiError when parsed_result fails the contract guard", async () => {
    const body = reviewResponseBody({ parsed_result: { not: "a valid contract" } });
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => body,
    });
    vi.stubGlobal("fetch", fetchMock);

    await expect(fetchReview("http://127.0.0.1:18080", "record-1")).rejects.toMatchObject({
      code: "invalid_review_payload",
    });
    await expect(fetchReview("http://127.0.0.1:18080", "record-1")).rejects.toBeInstanceOf(ApiError);
  });

  it("saveReviewDraft sends PUT .../review-draft with JSON body and returns the saved status", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ saved: true, import_status: "待校对" }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const result = await saveReviewDraft("http://127.0.0.1:18080", "record-1", minimalParsedResult);

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/import-records/record-1/review-draft", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(minimalParsedResult),
    });
    expect(result).toEqual({ saved: true, import_status: "待校对" });
  });

  it("runPreflight sends POST .../preflight-confirm with no body and returns the preflight report", async () => {
    const reportBody = {
      can_confirm: true,
      requires_revision_confirmation: false,
      blocking_errors: [],
      warnings: [],
    };
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => reportBody,
    });
    vi.stubGlobal("fetch", fetchMock);

    const report = await runPreflight("http://127.0.0.1:18080", "record-1");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/import-records/record-1/preflight-confirm", {
      method: "POST",
    });
    expect(report).toEqual(reportBody);
  });

  it("confirmImport sends POST .../confirm with the confirm_revision/confirmation_note body", async () => {
    const confirmResponseBody = {
      confirmed: true,
      inspection_year_id: "year-1",
      version_number: 2,
      written: {
        defect_observations: 3,
        defect_measurements: 1,
        defect_photos: 2,
        condition_ratings: 4,
      },
    };
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => confirmResponseBody,
    });
    vi.stubGlobal("fetch", fetchMock);

    const requestBody = { confirm_revision: true, confirmation_note: "确认修订版" };
    const result = await confirmImport("http://127.0.0.1:18080", "record-1", requestBody);

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/import-records/record-1/confirm", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(requestBody),
    });
    expect(result).toEqual(confirmResponseBody);
  });

  it("cancelImport sends POST .../cancel with no body and returns cancelled status", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ cancelled: true }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const result = await cancelImport("http://127.0.0.1:18080", "record-1");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/import-records/record-1/cancel", {
      method: "POST",
    });
    expect(result).toEqual({ cancelled: true });
  });

  it("throws ApiError with code/message parsed from a plain {code,message} error body", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 409,
      json: async () => ({ code: "import_record_not_editable", message: "导入记录状态已变化，无法保存草稿。" }),
    });
    vi.stubGlobal("fetch", fetchMock);

    await expect(saveReviewDraft("http://127.0.0.1:18080", "record-1", minimalParsedResult)).rejects.toMatchObject({
      code: "import_record_not_editable",
      message: "导入记录状态已变化，无法保存草稿。",
    });
  });

  it("400 with issues surfaces issues on the ApiError", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 400,
      json: async () => ({
        code: "contract_validation_failed",
        message: "候选数据不符合契约。",
        issues: [{ path: "defects[0].confidence", message: "must be between 0 and 1" }],
      }),
    });
    vi.stubGlobal("fetch", fetchMock);

    await expect(saveReviewDraft("http://127.0.0.1:18080", "record-1", minimalParsedResult)).rejects.toMatchObject({
      code: "contract_validation_failed",
      issues: [{ path: "defects[0].confidence", message: "must be between 0 and 1" }],
    });
  });

  it("confirmImport on 409 with a bare PreflightReport body (no code) throws ApiError relabeled preflight_failed with details", async () => {
    const preflightReportBody = {
      can_confirm: false,
      requires_revision_confirmation: false,
      blocking_errors: [
        { code: "candidate_pending_review", message: "仍有候选处于待确认状态", target_candidate_id: "defect_0001" },
      ],
      warnings: [],
    };
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 409,
      json: async () => preflightReportBody,
    });
    vi.stubGlobal("fetch", fetchMock);

    const error = await confirmImport("http://127.0.0.1:18080", "record-1", {
      confirm_revision: false,
      confirmation_note: "",
    }).catch((caught: unknown) => caught);

    expect(error).toBeInstanceOf(ApiError);
    const apiError = error as InstanceType<typeof ApiError>;
    // confirm 端点按 details 形状把中性 code 重标为 preflight 专属 code。
    expect(apiError.code).toBe("preflight_failed");
    expect(apiError.details).toEqual(preflightReportBody);
  });

  it("confirmImport on a 409 with a plain {code,message} body keeps that code (not preflight_failed)", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 409,
      json: async () => ({
        code: "revision_confirmation_required",
        message: "同桥同年已有当前有效事实，需显式确认修订版。",
      }),
    });
    vi.stubGlobal("fetch", fetchMock);

    await expect(
      confirmImport("http://127.0.0.1:18080", "record-1", { confirm_revision: false, confirmation_note: "" })
    ).rejects.toMatchObject({ code: "revision_confirmation_required" });
  });
});
