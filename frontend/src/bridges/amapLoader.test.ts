import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { loadAMap, resetAMapLoaderForTests, type AMapNamespace } from "./amapLoader";

const { fetchMapConfig } = vi.hoisted(() => ({ fetchMapConfig: vi.fn() }));
vi.mock("../api/mapApi", () => ({ fetchMapConfig }));

function script(): HTMLScriptElement {
  const element = document.head.querySelector<HTMLScriptElement>('script[src^="https://webapi.amap.com/"]');
  expect(element).not.toBeNull();
  return element!;
}

describe("AMap loader", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    resetAMapLoaderForTests();
    delete window.AMap;
    delete window._AMapSecurityConfig;
    fetchMapConfig.mockReset().mockResolvedValue({ available: true, js_key: "test-key", security_js_code: null });
  });
  afterEach(() => {
    document.head.querySelectorAll('script[src^="https://webapi.amap.com/"]').forEach((element) => element.remove());
    vi.clearAllTimers();
    vi.useRealTimers();
    delete window.AMap;
  });

  it("shares one request and resolves all callers", async () => {
    const first = loadAMap();
    expect(loadAMap()).toBe(first);
    await Promise.resolve();
    expect(fetchMapConfig).toHaveBeenCalledTimes(1);
    const namespace = {} as AMapNamespace;
    window.AMap = namespace;
    script().dispatchEvent(new Event("load"));
    expect(await first).toBe(namespace);
    expect(vi.getTimerCount()).toBe(0);
    expect(await loadAMap()).toBe(namespace);
  });

  it("removes a failed script and permits a fresh retry", async () => {
    const first = loadAMap();
    const failure = expect(first).rejects.toThrow("map_script_unreachable");
    await Promise.resolve();
    const failedScript = script();
    failedScript.dispatchEvent(new Event("error"));
    await failure;
    expect(failedScript.isConnected).toBe(false);
    expect(failedScript.onload).toBeNull();
    const second = loadAMap();
    await Promise.resolve();
    expect(script()).not.toBe(failedScript);
    window.AMap = {} as AMapNamespace;
    script().dispatchEvent(new Event("load"));
    await second;
    expect(fetchMapConfig).toHaveBeenCalledTimes(2);
  });

  it("removes a timed out script and its callbacks", async () => {
    const promise = loadAMap();
    const failure = expect(promise).rejects.toThrow("map_script_timeout");
    await Promise.resolve();
    const element = script();
    await vi.advanceTimersByTimeAsync(8000);
    await failure;
    expect(element.isConnected).toBe(false);
    expect(element.onload).toBeNull();
    expect(element.onerror).toBeNull();
    expect(vi.getTimerCount()).toBe(0);
  });

  it("rejects and removes a script that never defines AMap", async () => {
    const promise = loadAMap();
    const failure = expect(promise).rejects.toThrow("map_key_rejected");
    await Promise.resolve();
    const element = script();
    element.dispatchEvent(new Event("load"));
    await failure;
    expect(element.isConnected).toBe(false);
  });

  it("clears stale security configuration on a new attempt", async () => {
    window._AMapSecurityConfig = { securityJsCode: "stale" };
    const promise = loadAMap();
    await Promise.resolve();
    expect(window._AMapSecurityConfig.securityJsCode).toBe("");
    window.AMap = {} as AMapNamespace;
    script().dispatchEvent(new Event("load"));
    await promise;
  });

  it("a late failure cannot clear a newer pending attempt", async () => {
    let rejectOld!: (error: Error) => void;
    fetchMapConfig.mockImplementationOnce(() => new Promise((_resolve, reject) => { rejectOld = reject; }));
    const first = loadAMap();
    const failure = expect(first).rejects.toThrow("old request failed");
    resetAMapLoaderForTests();
    const second = loadAMap();
    await Promise.resolve();
    rejectOld(new Error("old request failed"));
    await failure;
    expect(loadAMap()).toBe(second);
    window.AMap = {} as AMapNamespace;
    script().dispatchEvent(new Event("load"));
    await second;
  });
});
