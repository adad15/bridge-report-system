import { afterEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "./apiClient";
import {
  createInspectionYear,
  deleteInspectionYear,
  deleteImportRecord,
  fetchImportRecordDeletionImpact,
  fetchInspectionYearDeletionImpact,
  fetchBridgeOverview,
  fetchRatingTreeVersions,
  uploadWordImport,
  workspaceErrorMessage,
} from "./workspaceApi";

describe("workspaceApi", () => {
  afterEach(() => vi.restoreAllMocks());

  it("URL-encodes workspace identifiers", async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => ({ bridge: {} }) });
    vi.stubGlobal("fetch", fetchMock);
    await fetchBridgeOverview("http://backend", "bridge/weird id");
    expect(fetchMock).toHaveBeenCalledWith("http://backend/api/bridges/bridge%2Fweird%20id/overview");
  });

  it("creates an annual inspection with JSON", async () => {
    const inspection = { id: "year-1", inspection_year: 2027 };
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 201,
      json: async () => ({ inspection_year: inspection }),
    });
    vi.stubGlobal("fetch", fetchMock);
    const input = {
      inspection_year: 2027,
      rating_tree_version_id: "rating-tree-1",
    };
    await expect(createInspectionYear("http://backend", "bridge-1", input)).resolves.toEqual(inspection);
    expect(fetchMock).toHaveBeenCalledWith("http://backend/api/bridges/bridge-1/inspection-years", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(input),
    });
  });

  it("loads published rating tree versions for annual creation", async () => {
    const versions = [{ id: "tree-1", tree_name: "单位桥梁有效评定树" }];
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ versions }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(fetchRatingTreeVersions("http://backend")).resolves.toEqual(versions);
    expect(fetchMock).toHaveBeenCalledWith("http://backend/api/rating-trees");
  });

  it("uploads a FormData body without manually setting multipart Content-Type", async () => {
    const imported = { id: "import-1", import_status: "已上传" };
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 201,
      json: async () => ({ import_record: imported }),
    });
    vi.stubGlobal("fetch", fetchMock);
    const file = new File(["docx"], "报告.docx");
    await expect(uploadWordImport("http://backend", "year/1", file, "正式Word")).resolves.toEqual(imported);
    const [, init] = fetchMock.mock.calls[0];
    expect(fetchMock.mock.calls[0][0]).toBe("http://backend/api/inspection-years/year%2F1/import-records/word");
    expect(init.method).toBe("POST");
    expect(init.body).toBeInstanceOf(FormData);
    expect(init.headers).toBeUndefined();
    expect(init.body.get("file")).toBe(file);
    expect(init.body.get("source_type")).toBe("正式Word");
  });

  it("previews and permanently deletes an encoded inspection year", async () => {
    const impact = { impact_token: "sha256:abc", confirmation_text: "永久删除 2026" };
    const result = { deleted: true, next_inspection_year_id: "year-2025" };
    const fetchMock = vi.fn()
      .mockResolvedValueOnce({ ok: true, status: 200, json: async () => impact })
      .mockResolvedValueOnce({ ok: true, status: 200, json: async () => result });
    vi.stubGlobal("fetch", fetchMock);
    await expect(fetchInspectionYearDeletionImpact("http://backend", "year/2026")).resolves.toEqual(impact);
    await expect(deleteInspectionYear("http://backend", "year/2026", {
      impact_token: "sha256:abc", confirmation_text: "永久删除 2026", reason: "误建年度",
    })).resolves.toEqual(result);
    expect(fetchMock.mock.calls[1]).toEqual([
      "http://backend/api/inspection-years/year%2F2026",
      { method: "DELETE", headers: { "Content-Type": "application/json" }, body: JSON.stringify({
        impact_token: "sha256:abc", confirmation_text: "永久删除 2026", reason: "误建年度",
      }) },
    ]);
  });

  it("previews and permanently deletes an encoded import record", async () => {
    const impact = { impact_token: "sha256:import", confirmation_text: "永久删除 DRJL-000001" };
    const result = { deleted: true, deletion_audit_id: "audit-1" };
    const fetchMock = vi.fn()
      .mockResolvedValueOnce({ ok: true, status: 200, json: async () => impact })
      .mockResolvedValueOnce({ ok: true, status: 200, json: async () => result });
    vi.stubGlobal("fetch", fetchMock);

    await expect(fetchImportRecordDeletionImpact("http://backend", "import/1")).resolves.toEqual(impact);
    await expect(deleteImportRecord("http://backend", "import/1", {
      impact_token: "sha256:import",
      confirmation_text: "永久删除 DRJL-000001",
      reason: "重复上传",
    })).resolves.toEqual(result);
    expect(fetchMock.mock.calls[1]).toEqual([
      "http://backend/api/import-records/import%2F1",
      { method: "DELETE", headers: { "Content-Type": "application/json" }, body: JSON.stringify({
        impact_token: "sha256:import",
        confirmation_text: "永久删除 DRJL-000001",
        reason: "重复上传",
      }) },
    ]);
  });

  it("maps stable errors and preserves unknown backend messages", () => {
    expect(workspaceErrorMessage(new ApiError("rating_tree_required", "raw"))).toContain("评定树");
    expect(workspaceErrorMessage(new ApiError("rating_tree_not_found", "raw"))).toContain("不存在");
    expect(workspaceErrorMessage(new ApiError("rating_tree_unavailable", "raw"))).toContain("不可用");
    expect(workspaceErrorMessage(new ApiError("invalid_word_file", "raw"))).toContain(".docx");
    expect(workspaceErrorMessage(new ApiError("word_upload_failed", "raw"))).toContain("Word 上传处理失败");
    expect(workspaceErrorMessage(new ApiError("import_record_edit_locked", "raw"))).toContain("其他人编辑");
    expect(workspaceErrorMessage(new ApiError("future_error", "后端原始提示"))).toBe("后端原始提示");
  });
});
