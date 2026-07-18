import { afterEach, describe, expect, it, vi } from "vitest";

import {
  confirmComponentInventory,
  deleteComponentInventoryEntry,
  fetchLatestComponentInventory,
  generateComponentInventory,
  setComponentInventoryMapping,
  updateComponentInventoryEntry,
} from "./componentInventoryApi";

describe("componentInventoryApi", () => {
  afterEach(() => vi.restoreAllMocks());

  it("loads the latest revision and encodes bridge ids", async () => {
    const revision = { id: "revision-1", entries: [] };
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ revision }),
    });
    vi.stubGlobal("fetch", fetchMock);

    await expect(fetchLatestComponentInventory("http://backend", "bridge/1")).resolves.toEqual(revision);
    expect(fetchMock).toHaveBeenCalledWith(
      "http://backend/api/bridges/bridge%2F1/component-inventories/latest"
    );
  });

  it("generates a draft with the complete template contract", async () => {
    const revision = { id: "revision-1", entries: [] };
    const input = {
      standard_package_id: "package-1",
      template_id: "template-1",
      bridge_type_id: "beam",
      span_count: 2,
      input_quantities: { span_count: 2, girders: 3 },
      groups: [
        {
          site_component_type: "主梁",
          site_name: "主梁",
          standard_component_category_id: "category-1",
          structure_part: "superstructure" as const,
          numbering_mode: "span_member" as const,
          quantity: 3,
          quantity_key: "girders",
        },
      ],
    };
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 201, json: async () => ({ revision }) });
    vi.stubGlobal("fetch", fetchMock);

    await expect(generateComponentInventory("http://backend", "bridge-1", input)).resolves.toEqual(revision);
    expect(fetchMock).toHaveBeenCalledWith(
      "http://backend/api/bridges/bridge-1/component-inventories/generate",
      { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(input) }
    );
  });

  it("updates, maps, confirms, and deletes through revision-scoped endpoints", async () => {
    const revision = { id: "revision/1", entries: [] };
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => ({ revision }) });
    vi.stubGlobal("fetch", fetchMock);

    await updateComponentInventoryEntry("http://backend", "revision/1", "entry/1", {
      component_number: "1-1#",
      site_name: "主梁",
      site_component_type: "主梁",
    });
    await setComponentInventoryMapping("http://backend", "revision/1", "entry/1", {
      standard_package_id: "package-1",
      standard_bridge_type_id: "beam",
      standard_component_category_id: "category-1",
      structure_part: "superstructure",
    });
    await confirmComponentInventory("http://backend", "revision/1", "已核对");
    await deleteComponentInventoryEntry("http://backend", "revision/1", "entry/1");

    expect(fetchMock.mock.calls.map(([url]) => url)).toEqual([
      "http://backend/api/component-inventories/revision%2F1/entries/entry%2F1",
      "http://backend/api/component-inventories/revision%2F1/entries/entry%2F1/mapping",
      "http://backend/api/component-inventories/revision%2F1/confirm",
      "http://backend/api/component-inventories/revision%2F1/entries/entry%2F1",
    ]);
    expect(fetchMock.mock.calls[1][1].method).toBe("PUT");
    expect(fetchMock.mock.calls[2][1].body).toBe(JSON.stringify({ note: "已核对" }));
    expect(fetchMock.mock.calls[3][1]).toEqual({ method: "DELETE" });
  });
});
