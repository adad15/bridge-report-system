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
    }, "rev-1", "lock-1");
    await markComponentMissing(
      "http://backend", "i1", { part_name: "支座", component_number: "2-1#支座" }, "rev-1", "lock-1");
    await clearComponentBinding(
      "http://backend", "i1", { part_name: "上部承重构件", component_number: "1-1#梁" }, "rev-1", "lock-1");

    expect(fetchMock.mock.calls.map(([url]) => url)).toEqual([
      "http://backend/api/import-records/i1/component-binding/bind",
      "http://backend/api/import-records/i1/component-binding/mark-missing",
      "http://backend/api/import-records/i1/component-binding/clear",
    ]);
    // 每条写请求都必须带上依据的台账版本，漏掉任何一条都会让后端自己挑版本。
    for (const call of fetchMock.mock.calls) {
      expect(JSON.parse((call[1] as RequestInit).body as string).expected_inventory_revision_id)
        .toBe("rev-1");
    }
    expect(JSON.parse((fetchMock.mock.calls[0][1] as RequestInit).body as string)).toEqual({
      part_name: "上部承重构件",
      component_number: "1-1#梁",
      bridge_component_id: "c1",
      expected_inventory_revision_id: "rev-1",
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

    await previewComponentRangeSplit("http://backend", "i1", targets, "rev-1");
    await applyComponentRangeSplit("http://backend", "i1", targets, "sha256:preview", "rev-1", "lock-1");

    expect(fetchMock.mock.calls.map(([url]) => url)).toEqual([
      "http://backend/api/import-records/i1/component-binding/split-preview",
      "http://backend/api/import-records/i1/component-binding/split-apply",
    ]);
    expect(JSON.parse((fetchMock.mock.calls[1][1] as RequestInit).body as string)).toEqual({
      targets,
      impact_token: "sha256:preview",
      expected_inventory_revision_id: "rev-1",
    });
    // 预览也要带：后端据此判断这次预览依据的台账是不是还有效。
    expect(JSON.parse((fetchMock.mock.calls[0][1] as RequestInit).body as string)
      .expected_inventory_revision_id).toBe("rev-1");
  });
});
