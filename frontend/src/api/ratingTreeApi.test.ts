import { beforeEach, describe, expect, it, vi } from "vitest";

import {
  clearRatingTreeApiCacheForTests,
  fetchApplicableRatingTreeDefects,
  fetchRatingTreeChildren,
  fetchRatingTreeNode,
  fetchRatingTreeVersion,
  searchRatingTree,
} from "./ratingTreeApi";

describe("ratingTreeApi", () => {
  beforeEach(() => {
    vi.restoreAllMocks();
    clearRatingTreeApiCacheForTests();
  });

  it("caches immutable version, children and node detail requests", async () => {
    const fetchMock = vi.spyOn(globalThis, "fetch")
      .mockResolvedValueOnce({
        ok: true,
        json: async () => ({ version: { id: "v1", tree_name: "单位桥梁评定树" } }),
      } as Response)
      .mockResolvedValueOnce({
        ok: true,
        json: async () => ({ nodes: [{ id: "n1", display_name: "梁式桥" }] }),
      } as Response)
      .mockResolvedValueOnce({
        ok: true,
        json: async () => ({ node: { id: "n1", display_name: "梁式桥" } }),
      } as Response);

    await fetchRatingTreeVersion("http://backend", "v1");
    await fetchRatingTreeVersion("http://backend", "v1");
    await fetchRatingTreeChildren("http://backend", "v1", null);
    await fetchRatingTreeChildren("http://backend", "v1", null);
    await fetchRatingTreeNode("http://backend", "v1", "n1");
    await fetchRatingTreeNode("http://backend", "v1", "n1");

    expect(fetchMock).toHaveBeenCalledTimes(3);
    expect(fetchMock.mock.calls[1][0]).toBe(
      "http://backend/api/rating-trees/v1/nodes?parent_id=root&limit=200",
    );
  });

  it("encodes search and applicable-scope query parameters", async () => {
    const fetchMock = vi.spyOn(globalThis, "fetch").mockResolvedValue({
      ok: true,
      json: async () => ({ nodes: [] }),
    } as Response);

    await searchRatingTree("http://backend", "v1", "渗水 泛碱");
    await fetchApplicableRatingTreeDefects(
      "http://backend",
      "v1",
      "h21.bridge_type.beam",
      "h21.component.main_girder",
    );

    expect(fetchMock.mock.calls[0][0]).toContain("q=%E6%B8%97%E6%B0%B4+%E6%B3%9B%E7%A2%B1");
    expect(fetchMock.mock.calls[1][0]).toContain("bridge_type_id=h21.bridge_type.beam");
    expect(fetchMock.mock.calls[1][0]).toContain("component_category_id=h21.component.main_girder");
  });
});
