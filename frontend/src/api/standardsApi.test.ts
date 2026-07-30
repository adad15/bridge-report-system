import { afterEach, describe, expect, it, vi } from "vitest";

import {
  fetchStandardCatalog,
  fetchStandardMappingCatalogs,
  fetchStandardPackages,
  setStandardPackageEnabled,
} from "./standardsApi";

describe("standardsApi", () => {
  afterEach(() => vi.restoreAllMocks());

  it("lists installed packages", async () => {
    const packages = [{ id: "package-1", family: "technical_condition" }];
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true, status: 200, json: async () => ({ packages }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(fetchStandardPackages("http://backend")).resolves.toEqual(packages);
    expect(fetchMock).toHaveBeenCalledWith("http://backend/api/standards");
  });

  it("loads the lightweight technical mapping catalogs in one request", async () => {
    const catalogs = [{ package: { id: "package-1" }, component_categories: [] }];
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true, status: 200, json: async () => ({ catalogs }),
    });
    vi.stubGlobal("fetch", fetchMock);

    await expect(fetchStandardMappingCatalogs("http://backend")).resolves.toEqual(catalogs);
    expect(fetchMock).toHaveBeenCalledWith(
      "http://backend/api/standards/technical-mapping-catalogs"
    );
  });

  it("encodes catalog ids and changes enabled state with PATCH", async () => {
    const catalog = { package: { id: "package/1" }, bridge_types: [] };
    const updated = { id: "package/1", is_enabled: false };
    const fetchMock = vi.fn()
      .mockResolvedValueOnce({ ok: true, status: 200, json: async () => catalog })
      .mockResolvedValueOnce({ ok: true, status: 200, json: async () => ({ package: updated }) });
    vi.stubGlobal("fetch", fetchMock);

    await expect(fetchStandardCatalog("http://backend", "package/1")).resolves.toEqual(catalog);
    await expect(setStandardPackageEnabled("http://backend", "package/1", false)).resolves.toEqual(updated);
    expect(fetchMock.mock.calls[0][0]).toBe("http://backend/api/standards/package%2F1/catalog");
    expect(fetchMock.mock.calls[1]).toEqual([
      "http://backend/api/standards/package%2F1/enabled",
      { method: "PATCH", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ enabled: false }) },
    ]);
  });
});
