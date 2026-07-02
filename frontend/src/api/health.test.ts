import { afterEach, describe, expect, it, vi } from "vitest";

import { fetchBackendHealth } from "./health";

describe("fetchBackendHealth", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("fetches C++ backend health from the configured base URL", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        status: "ok",
        service: "bridge-report-cpp-backend",
        version: "0.1.0",
      }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const health = await fetchBackendHealth("http://127.0.0.1:18080");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/health");
    expect(health.status).toBe("ok");
    expect(health.service).toBe("bridge-report-cpp-backend");
  });
});
