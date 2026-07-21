import { beforeEach, describe, expect, it, vi } from "vitest";
import { advanceBridgeCleanup, createBridge, deleteBridges, fetchBridgeDeletionImpact } from "./bridgeAdministrationApi";

describe("bridgeAdministrationApi", () => {
  beforeEach(() => vi.stubGlobal("fetch", vi.fn()));
  it("uses explicit POST and DELETE JSON contracts", async () => {
    vi.mocked(fetch)
      .mockResolvedValueOnce(new Response(JSON.stringify({ bridge: { id: "b1" } }), { status: 201 }))
      .mockResolvedValueOnce(new Response(JSON.stringify({ bridges: [], totals: {}, confirmation_text: "永久删除 QL-1" }), { status: 200 }))
      .mockResolvedValueOnce(new Response(JSON.stringify({ batch_id: "x", results: [] }), { status: 200 }));
    await createBridge("http://backend", { bridge_name: "测试桥", status: "在用" });
    await fetchBridgeDeletionImpact("http://backend", ["b1"]);
    await deleteBridges("http://backend", { reason: "误建", confirmation_text: "永久删除 QL-1", items: [{ bridge_id: "b1", impact_token: "sha256:x" }] });
    expect(vi.mocked(fetch).mock.calls[0][1]).toMatchObject({ method: "POST" });
    expect(vi.mocked(fetch).mock.calls[1][0]).toBe("http://backend/api/bridges/deletion-impact");
    expect(vi.mocked(fetch).mock.calls[2][1]).toMatchObject({ method: "DELETE" });
  });

  it("advances bridge file cleanup with an encoded audit id", async () => {
    vi.mocked(fetch).mockResolvedValueOnce(
      new Response(JSON.stringify({ total: 10, completed: 4, failed: 0, pending: 6, done: false }), { status: 200 })
    );
    const progress = await advanceBridgeCleanup("http://backend", "audit/1");
    expect(vi.mocked(fetch).mock.calls[0][0]).toBe("http://backend/api/bridge-deletion-audits/audit%2F1/cleanup/advance");
    expect(vi.mocked(fetch).mock.calls[0][1]).toMatchObject({ method: "POST" });
    expect(progress).toEqual({ total: 10, completed: 4, failed: 0, pending: 6, done: false });
  });
});
