import { afterEach, describe, expect, it, vi } from "vitest";

import {
  confirmComponentInventory,
  confirmPendingComponentInventoryMappings,
  deleteComponentInventoryEntry,
  fetchPartCatalog,
  generateComponentInventory,
  setComponentInventoryMapping,
  updateComponentInventoryEntry,
} from "./componentInventoryApi";

describe("componentInventoryApi", () => {
  afterEach(() => vi.restoreAllMocks());

  it("fetches the part catalog for a bridge type", async () => {
    const parts = [
      {
        part_key: "beam.girder",
        default_name: "梁",
        structure_part: "superstructure" as const,
        standard_component_category_id: "h21.component.beam.upper_bearing",
        number_template: "{span}-{c1}#{name}",
        provisional: false,
        count_inputs: [{ key: "girders_per_span", label: "每孔梁片数" }],
      },
    ];
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => ({ parts }) });
    vi.stubGlobal("fetch", fetchMock);

    await expect(
      fetchPartCatalog("http://backend", "package-1", "h21.bridge_type.beam")
    ).resolves.toEqual(parts);
    expect(fetchMock).toHaveBeenCalledWith(
      "http://backend/api/component-inventories/part-catalog?standard_package_id=package-1&bridge_type_id=h21.bridge_type.beam"
    );
  });

  it("generates a draft from part_selections without legacy groups", async () => {
    // 生成台账现在也回传汇总形状，不再是整份修订版。
    const summary = {
      revision: { id: "revision-1", active_entry_count: 0 },
      groups: [], blockers: { total: 0, individual_total: 0, by_code: {}, samples: [] },
    };
    const input = {
      standard_package_id: "package-1",
      bridge_type_id: "h21.bridge_type.beam",
      span_count: 5,
      part_selections: [{ part_key: "beam.girder", site_name: "空心板", counts: [13] }],
    };
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 201, json: async () => summary });
    vi.stubGlobal("fetch", fetchMock);

    await expect(generateComponentInventory("http://backend", "bridge-1", input)).resolves.toEqual(summary);
    const body = JSON.parse((fetchMock.mock.calls[0][1] as RequestInit).body as string);
    expect(body.part_selections[0].part_key).toBe("beam.girder");
    expect(body.groups).toBeUndefined();
  });

  it("confirms pending mappings for one group or the whole revision", async () => {
    const revision = { id: "revision/1", entries: [] };
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => ({ revision }) });
    vi.stubGlobal("fetch", fetchMock);

    await confirmPendingComponentInventoryMappings("http://backend", "revision/1", "主梁");
    await confirmPendingComponentInventoryMappings("http://backend", "revision/1");

    expect(fetchMock.mock.calls.map(([url]) => url)).toEqual([
      "http://backend/api/component-inventories/revision%2F1/mappings/confirm-pending",
      "http://backend/api/component-inventories/revision%2F1/mappings/confirm-pending",
    ]);
    expect(fetchMock.mock.calls[0][1].method).toBe("POST");
    expect(fetchMock.mock.calls[0][1].body).toBe(JSON.stringify({ site_component_type: "主梁" }));
    expect(fetchMock.mock.calls[1][1].body).toBe(JSON.stringify({}));
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
