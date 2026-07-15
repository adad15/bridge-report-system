import { afterEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "./apiClient";
import {
  createInspectionYear,
  fetchBridgeOverview,
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
    await expect(createInspectionYear("http://backend", "bridge-1", 2027)).resolves.toEqual(inspection);
    expect(fetchMock).toHaveBeenCalledWith("http://backend/api/bridges/bridge-1/inspection-years", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ inspection_year: 2027 }),
    });
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

  it("maps stable errors and preserves unknown backend messages", () => {
    expect(workspaceErrorMessage(new ApiError("invalid_word_file", "raw"))).toContain(".docx");
    expect(workspaceErrorMessage(new ApiError("future_error", "后端原始提示"))).toBe("后端原始提示");
  });
});
