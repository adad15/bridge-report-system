import { beforeEach, describe, expect, it, vi } from "vitest";

import { fetchResolutionWorkspace } from "./resolutionApi";

// 工作区读模型被三个调用方各取一次（页面壳、构件绑定面板、病害与照片面板），加载时
// 几乎同时发出。它们要的是同一份快照，应该合并成一次请求。
describe("fetchResolutionWorkspace", () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  function okResponse(draftVersion: number) {
    return {
      ok: true,
      json: async () => ({
        import_record_id: "record-1",
        draft_version: draftVersion,
        groups: [],
        parts: [],
        progress: {},
      }),
    } as Response;
  }

  it("merges concurrent callers into a single request", async () => {
    const fetchMock = vi.spyOn(globalThis, "fetch").mockResolvedValue(okResponse(1));

    const [a, b, c] = await Promise.all([
      fetchResolutionWorkspace("http://backend", "record-1"),
      fetchResolutionWorkspace("http://backend", "record-1"),
      fetchResolutionWorkspace("http://backend", "record-1"),
    ]);

    expect(fetchMock).toHaveBeenCalledTimes(1);
    // 三个调用方拿到的必须是同一份，而不是各自一份可能不一致的快照。
    expect(a).toBe(b);
    expect(b).toBe(c);
  });

  /* 这条是防"顺手改成缓存"的：合并只对同时在飞的请求成立。绑定完成后的重取必须
     真的重新发，拿缓存会让页面停在写操作之前的状态——那正是这份数据最不能出错的时刻。 */
  it("does not cache: a call after the first settles fetches again", async () => {
    const fetchMock = vi.spyOn(globalThis, "fetch")
      .mockResolvedValueOnce(okResponse(1))
      .mockResolvedValueOnce(okResponse(2));

    const first = await fetchResolutionWorkspace("http://backend", "record-1");
    const second = await fetchResolutionWorkspace("http://backend", "record-1");

    expect(fetchMock).toHaveBeenCalledTimes(2);
    expect(first.draft_version).toBe(1);
    expect(second.draft_version).toBe(2);
  });

  it("keeps different import records apart", async () => {
    const fetchMock = vi.spyOn(globalThis, "fetch").mockResolvedValue(okResponse(1));

    await Promise.all([
      fetchResolutionWorkspace("http://backend", "record-1"),
      fetchResolutionWorkspace("http://backend", "record-2"),
    ]);

    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  // 失败也要把在途记录摘掉，否则这个导入记录后续再也取不到新数据。
  it("clears the in-flight entry when the request fails", async () => {
    const fetchMock = vi.spyOn(globalThis, "fetch")
      .mockRejectedValueOnce(new Error("network down"))
      .mockResolvedValueOnce(okResponse(3));

    await expect(fetchResolutionWorkspace("http://backend", "record-1")).rejects.toThrow();
    const retried = await fetchResolutionWorkspace("http://backend", "record-1");

    expect(fetchMock).toHaveBeenCalledTimes(2);
    expect(retried.draft_version).toBe(3);
  });
});
