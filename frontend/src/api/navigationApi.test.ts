import { afterEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "./navigationApi";
import { fetchBridges, fetchImportRecords, fetchInspectionYears } from "./navigationApi";

describe("navigationApi", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("fetchBridges requests GET /api/bridges and returns the bridges array", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({
        bridges: [
          {
            id: "bridge-1",
            system_number: "QL-000001",
            bridge_name: "绕阳河二号桥",
            route_name: "S101",
            status: "在用",
          },
        ],
      }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const bridges = await fetchBridges("http://127.0.0.1:18080");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/bridges");
    expect(bridges).toEqual([
      {
        id: "bridge-1",
        system_number: "QL-000001",
        bridge_name: "绕阳河二号桥",
        route_name: "S101",
        status: "在用",
      },
    ]);
  });

  it("fetchInspectionYears requests GET /api/bridges/{bridgeId}/inspection-years and returns the years array", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({
        inspection_years: [
          {
            id: "year-1",
            system_number: "NY-000001",
            inspection_year: 2026,
            status: "已确认",
            version_number: 1,
            is_current: true,
          },
        ],
      }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const years = await fetchInspectionYears("http://127.0.0.1:18080", "bridge-1");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/bridges/bridge-1/inspection-years");
    expect(years).toEqual([
      {
        id: "year-1",
        system_number: "NY-000001",
        inspection_year: 2026,
        status: "已确认",
        version_number: 1,
        is_current: true,
      },
    ]);
  });

  it("fetchImportRecords requests GET /api/bridges/{bridgeId}/import-records and returns the records array", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({
        import_records: [
          {
            id: "record-1",
            system_number: "IMP-000001",
            import_name: "2026年度检查报告.docx",
            source_type: "软件导出Word",
            import_status: "待校对",
            inspection_year_id: null,
            importer_name: "张三",
            created_at: "2026-07-01T00:00:00+08:00",
          },
        ],
      }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const records = await fetchImportRecords("http://127.0.0.1:18080", "bridge-1");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/bridges/bridge-1/import-records");
    expect(records).toEqual([
      {
        id: "record-1",
        system_number: "IMP-000001",
        import_name: "2026年度检查报告.docx",
        source_type: "软件导出Word",
        import_status: "待校对",
        inspection_year_id: null,
        importer_name: "张三",
        created_at: "2026-07-01T00:00:00+08:00",
      },
    ]);
  });

  it("URL-encodes the bridgeId path segment", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ import_records: [] }),
    });
    vi.stubGlobal("fetch", fetchMock);

    await fetchImportRecords("http://127.0.0.1:18080", "bridge/weird id");

    expect(fetchMock).toHaveBeenCalledWith(
      "http://127.0.0.1:18080/api/bridges/bridge%2Fweird%20id/import-records"
    );
  });

  it("throws ApiError with the code/message parsed from a non-2xx body", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 404,
      json: async () => ({ code: "bridge_not_found", message: "指定的桥梁不存在" }),
    });
    vi.stubGlobal("fetch", fetchMock);

    await expect(fetchInspectionYears("http://127.0.0.1:18080", "missing-bridge")).rejects.toMatchObject({
      code: "bridge_not_found",
      message: "指定的桥梁不存在",
    });
    await expect(fetchInspectionYears("http://127.0.0.1:18080", "missing-bridge")).rejects.toBeInstanceOf(ApiError);
  });
});
