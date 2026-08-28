import { afterEach, describe, expect, it, vi } from "vitest";

import type { BridgeAnnualInspectionData } from "../contracts/annualInspection";
import { ApiError } from "./apiClient";
import {
  acquireEditLock,
  cancelImport,
  confirmImport,
  fetchReview,
  forceReleaseEditLock,
  heartbeatEditLock,
  parseWordImport,
  photoContentUrl,
  releaseEditLock,
  runPreflight,
  saveReviewDraft,
} from "./reviewApi";

// 满足 isBridgeAnnualInspectionData 最小必填字段集合的候选数据骨架，供测试复用。
const minimalWireResult: BridgeAnnualInspectionData = {
  contract: {
    name: "BridgeAnnualInspectionData",
    version: "5.0",
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
  comparison_candidates: [],
  report_text_candidates: [],
  warnings: [],
  errors: [],
};

const minimalParsedResult = minimalWireResult;

function reviewResponseBody(overrides: Partial<Record<string, unknown>> = {}) {
  return {
    import_record: {
      id: "record-1",
      system_number: "IMP-000001",
      import_name: "2026年度检查报告.docx",
      source_type: "软件导出Word",
      import_status: "待校对",
      draft_version: 1,
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
    parsed_result: minimalWireResult,
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
    contract_compatibility: "native_4_0",
    technical_condition_standard: null,
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
    const body = reviewResponseBody({});
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
    expect(review.contract_compatibility).toBe("native_4_0");
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

    const result = await saveReviewDraft("http://127.0.0.1:18080", "record-1", minimalParsedResult, "lock-token", 1);

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/import-records/record-1/review-draft", expect.objectContaining({
      method: "PUT",
      body: JSON.stringify(minimalParsedResult),
    }));
    const saveHeaders = new Headers(fetchMock.mock.calls[0][1].headers);
    expect(saveHeaders.get("Content-Type")).toBe("application/json");
    expect(saveHeaders.get("X-Edit-Lock-Token")).toBe("lock-token");
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

    const report = await runPreflight("http://127.0.0.1:18080", "record-1", "lock-token");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/import-records/record-1/preflight-confirm", expect.objectContaining({
      method: "POST",
    }));
    expect(new Headers(fetchMock.mock.calls[0][1].headers).get("X-Edit-Lock-Token")).toBe("lock-token");
    expect(report).toEqual(reportBody);
  });

  it("confirmImport sends POST .../confirm with the confirm_revision/confirmation_note body", async () => {
    const confirmResponseBody = {
      confirmed: true,
      inspection_year_id: "year-1",
      version_number: 2,
      assessment_run_id: "run-1",
      written: {
        defect_observations: 3,
        defect_measurements: 1,
        defect_photos: 2,
        condition_ratings: 4,
        assessment_component_results: 3,
        assessment_part_results: 4,
        assessment_control_results: 1,
        assessment_rule_traces: 9,
      },
    };
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => confirmResponseBody,
    });
    vi.stubGlobal("fetch", fetchMock);

    const requestBody = { confirm_revision: true, confirmation_note: "确认修订版" };
    const result = await confirmImport("http://127.0.0.1:18080", "record-1", requestBody, "lock-token");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/import-records/record-1/confirm", expect.objectContaining({
      method: "POST",
      body: JSON.stringify(requestBody),
    }));
    const confirmHeaders = new Headers(fetchMock.mock.calls[0][1].headers);
    expect(confirmHeaders.get("Content-Type")).toBe("application/json");
    expect(confirmHeaders.get("X-Edit-Lock-Token")).toBe("lock-token");
    expect(result).toEqual(confirmResponseBody);
  });

  it("cancelImport sends POST .../cancel with no body and returns cancelled status", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ cancelled: true }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const result = await cancelImport("http://127.0.0.1:18080", "record-1", "lock-token");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/import-records/record-1/cancel", expect.objectContaining({
      method: "POST",
    }));
    expect(new Headers(fetchMock.mock.calls[0][1].headers).get("X-Edit-Lock-Token")).toBe("lock-token");
    expect(result).toEqual({ cancelled: true });
  });

  it("throws ApiError with code/message parsed from a plain {code,message} error body", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 409,
      json: async () => ({ code: "import_record_not_editable", message: "导入记录状态已变化，无法保存草稿。" }),
    });
    vi.stubGlobal("fetch", fetchMock);

    await expect(saveReviewDraft("http://127.0.0.1:18080", "record-1", minimalParsedResult, "lock-token", 1)).rejects.toMatchObject({
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

    await expect(saveReviewDraft("http://127.0.0.1:18080", "record-1", minimalParsedResult, "lock-token", 1)).rejects.toMatchObject({
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
    }, "lock-token").catch((caught: unknown) => caught);

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
      confirmImport("http://127.0.0.1:18080", "record-1", { confirm_revision: false, confirmation_note: "" }, "lock-token")
    ).rejects.toMatchObject({ code: "revision_confirmation_required" });
  });

  it("acquires, heartbeats, releases, and force-releases an edit lock", async () => {
    const lock = {
      owner_username: "zhang",
      owner_display_name: "张工",
      acquired_at: "2026-07-15T10:00:00+08:00",
      expires_at: "2026-07-15T10:02:00+08:00",
      owned_by_current_user: true,
    };
    const fetchMock = vi.fn()
      .mockResolvedValueOnce({
        ok: true,
        status: 200,
        json: async () => ({ acquired: true, lock_token: "lock-token", heartbeat_interval_seconds: 30, lock }),
      })
      .mockResolvedValueOnce({ ok: true, status: 200, json: async () => ({ renewed: true, lock }) })
      .mockResolvedValueOnce({ ok: true, status: 200, json: async () => ({ released: true }) })
      .mockResolvedValueOnce({ ok: true, status: 200, json: async () => ({ released: true }) });
    vi.stubGlobal("fetch", fetchMock);

    await expect(acquireEditLock("http://127.0.0.1:18080", "record-1")).resolves.toMatchObject({ lock_token: "lock-token" });
    await expect(heartbeatEditLock("http://127.0.0.1:18080", "record-1", "lock-token")).resolves.toMatchObject({ renewed: true, lock });
    await expect(releaseEditLock("http://127.0.0.1:18080", "record-1", "lock-token")).resolves.toEqual({ released: true });
    await expect(forceReleaseEditLock("http://127.0.0.1:18080", "record-1", "交接给夜班人员")).resolves.toEqual({ released: true });

    expect(fetchMock).toHaveBeenNthCalledWith(1, "http://127.0.0.1:18080/api/import-records/record-1/edit-lock", { method: "POST" });
    expect(fetchMock).toHaveBeenNthCalledWith(2, "http://127.0.0.1:18080/api/import-records/record-1/edit-lock/heartbeat", expect.objectContaining({
      method: "POST",
    }));
    expect(new Headers(fetchMock.mock.calls[1][1].headers).get("X-Edit-Lock-Token")).toBe("lock-token");
    expect(fetchMock).toHaveBeenNthCalledWith(3, "http://127.0.0.1:18080/api/import-records/record-1/edit-lock", expect.objectContaining({
      method: "DELETE",
      keepalive: false,
    }));
    expect(new Headers(fetchMock.mock.calls[2][1].headers).get("X-Edit-Lock-Token")).toBe("lock-token");
    expect(fetchMock).toHaveBeenNthCalledWith(4, "http://127.0.0.1:18080/api/import-records/record-1/edit-lock/force-release", expect.objectContaining({
      method: "POST",
      body: JSON.stringify({ reason: "交接给夜班人员" }),
    }));
    expect(new Headers(fetchMock.mock.calls[3][1].headers).get("Content-Type")).toBe("application/json");
  });

  it("posts the fixed annual Word parse contract", async () => {
    const response = {
      parsed: true as const,
      temporary_photo_file_count: 36,
      photo_candidate_count: 31,
      archived_photo_count: 31,
    };
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => response });
    vi.stubGlobal("fetch", fetchMock);
    const body = {
      rule_profile: "辽宁国省干线" as const,
      import_mode: "已有桥年度导入" as const,
      file_role: "当前年度检测资料" as const,
      data_role: "当前年度" as const,
      inspection_date: "2026-05-18",
      report_number: "BG-2026-001",
      project_name: "绕阳河二号桥2026年度定期检测",
    };

    await expect(parseWordImport("http://127.0.0.1:18080", "record/1", body)).resolves.toEqual(response);
    expect(fetchMock).toHaveBeenCalledWith(
      "http://127.0.0.1:18080/api/import-records/record%2F1/parse-word",
      { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }
    );
  });
});
