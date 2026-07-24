import { afterEach, describe, expect, it, vi } from "vitest";

import {
  bindComponent,
  clearComponentBinding,
  fetchComponentBinding,
  markComponentMissing,
  previewComponentRangeSplit,
  applyComponentRangeSplit,
} from "./importBindingApi";

const overview = {
  inventory_confirmed: true,
  groups: [
    {
      part_name: "上部承重构件",
      total: 1,
      bound: 0,
      unmatched: 1,
      ambiguous: 0,
      missing: 0,
      rows: [
        {
          component_number: "1-1#梁",
          defect_count: 3,
          status: "unmatched" as const,
          bridge_component_id: null,
          candidate_component_ids: [],
        },
      ],
    },
  ],
};

describe("importBindingApi", () => {
  afterEach(() => vi.restoreAllMocks());

  it("loads the binding overview and encodes the import id", async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => ({ overview }) });
    vi.stubGlobal("fetch", fetchMock);

    await expect(fetchComponentBinding("http://backend", "import/1")).resolves.toEqual(overview);
    expect(fetchMock).toHaveBeenCalledWith(
      "http://backend/api/import-records/import%2F1/component-binding"
    );
  });

  it("binds, marks missing, and clears through POST endpoints", async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => ({ overview }) });
    vi.stubGlobal("fetch", fetchMock);

    await bindComponent("http://backend", "i1", {
      part_name: "上部承重构件",
      component_number: "1-1#梁",
      bridge_component_id: "c1",
    });
    await markComponentMissing("http://backend", "i1", { part_name: "支座", component_number: "2-1#支座" });
    await clearComponentBinding("http://backend", "i1", { part_name: "上部承重构件", component_number: "1-1#梁" });

    expect(fetchMock.mock.calls.map(([url]) => url)).toEqual([
      "http://backend/api/import-records/i1/component-binding/bind",
      "http://backend/api/import-records/i1/component-binding/mark-missing",
      "http://backend/api/import-records/i1/component-binding/clear",
    ]);
    expect(JSON.parse((fetchMock.mock.calls[0][1] as RequestInit).body as string)).toEqual({
      part_name: "上部承重构件",
      component_number: "1-1#梁",
      bridge_component_id: "c1",
    });
    expect((fetchMock.mock.calls[1][1] as RequestInit).method).toBe("POST");
  });

  it("previews and applies a range split with the impact token", async () => {
    const preview = {
      items: [],
      totals: {
        selected_range_count: 1, source_defect_count: 1, result_defect_count: 25,
        result_photo_count: 25, bound_count: 25, ambiguous_count: 0, unmatched_count: 0,
      },
      impact_token: "sha256:preview",
    };
    const fetchMock = vi.fn()
      .mockResolvedValueOnce({ ok: true, status: 200, json: async () => preview })
      .mockResolvedValueOnce({
        ok: true, status: 200,
        json: async () => ({ ...preview, operation_id: "op-1", overview }),
      });
    vi.stubGlobal("fetch", fetchMock);
    const targets = [{ part_name: "上部承重构件", component_number: "1-1#梁~1-25#梁" }];

    await previewComponentRangeSplit("http://backend", "i1", targets);
    await applyComponentRangeSplit("http://backend", "i1", targets, "sha256:preview");

    expect(fetchMock.mock.calls.map(([url]) => url)).toEqual([
      "http://backend/api/import-records/i1/component-binding/split-preview",
      "http://backend/api/import-records/i1/component-binding/split-apply",
    ]);
    expect(JSON.parse((fetchMock.mock.calls[1][1] as RequestInit).body as string)).toEqual({
      targets,
      impact_token: "sha256:preview",
    });
  });
});
