import { afterEach, describe, expect, it, vi } from "vitest";

import {
  bindObservationThread,
  createDefectThread,
  defectPhotoContentUrl,
  fetchComponents,
  fetchThreadSuggestions,
  fetchUnboundObservations,
  threadBindingErrorMessage,
} from "./componentArchiveApi";

function mockFetchOnce(body: unknown): ReturnType<typeof vi.fn> {
  const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => body });
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

describe("componentArchiveApi", () => {
  afterEach(() => {
    vi.restoreAllMocks();
    vi.unstubAllGlobals();
  });

  it("builds an encoded defect photo content URL", () => {
    expect(defectPhotoContentUrl("http://127.0.0.1:18080/", "a/b"))
      .toBe("http://127.0.0.1:18080/api/defect-photos/a%2Fb/content");
  });

  it("fetchComponents unwraps the components array", async () => {
    const fetchMock = mockFetchOnce({ components: [{ id: "c1" }] });

    const components = await fetchComponents("http://127.0.0.1:18080", "bridge-1");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/bridges/bridge-1/components");
    expect(components).toEqual([{ id: "c1" }]);
  });

  it("fetchUnboundObservations and fetchThreadSuggestions unwrap their arrays", async () => {
    mockFetchOnce({ unbound_observations: [{ id: "o1" }] });
    expect(await fetchUnboundObservations("http://x", "bridge-1")).toEqual([{ id: "o1" }]);

    mockFetchOnce({ suggestions: [{ id: "t1" }] });
    expect(await fetchThreadSuggestions("http://x", "obs-1")).toEqual([{ id: "t1" }]);
  });

  it("createDefectThread posts the body as JSON", async () => {
    const fetchMock = mockFetchOnce({ created: true, observation_id: "o1", observation_updated_at: "t" });

    await createDefectThread("http://x", {
      bridge_component_id: "c1",
      defect_type: "蜂窝、麻面",
      defect_location: "左侧端部",
      first_observation_id: "o1",
      expected_observation_updated_at: "token",
    });

    const [url, init] = fetchMock.mock.calls[0] as [string, RequestInit];
    expect(url).toBe("http://x/api/defect-threads");
    expect(init.method).toBe("POST");
    expect(JSON.parse(init.body as string).defect_location).toBe("左侧端部");
  });

  it("bindObservationThread puts the binding body with the concurrency token", async () => {
    const fetchMock = mockFetchOnce({ bound: true, observation_id: "o1", observation_updated_at: "t2" });

    await bindObservationThread("http://x", "o1", {
      defect_thread_id: "t1",
      expected_observation_updated_at: "t1-token",
      confirm_rebind: true,
    });

    const [url, init] = fetchMock.mock.calls[0] as [string, RequestInit];
    expect(url).toBe("http://x/api/defect-observations/o1/defect-thread");
    expect(init.method).toBe("PUT");
    expect(JSON.parse(init.body as string)).toEqual({
      defect_thread_id: "t1",
      expected_observation_updated_at: "t1-token",
      confirm_rebind: true,
    });
  });

  it("maps stable binding error codes to actionable Chinese messages", () => {
    expect(threadBindingErrorMessage("observation_revision_conflict", "fallback"))
      .toBe("该观测已被其他操作更新，请刷新页面后重试。");
    expect(threadBindingErrorMessage("unknown_code", "fallback")).toBe("fallback");
  });
});
