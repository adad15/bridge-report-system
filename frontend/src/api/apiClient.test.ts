import { afterEach, describe, expect, it, vi } from "vitest";

import { ApiError, request, setAuthToken, setUnauthorizedHandler } from "./apiClient";

describe("apiClient request/parseError", () => {
  afterEach(() => {
    vi.restoreAllMocks();
    setAuthToken(null);
    setUnauthorizedHandler(null);
  });

  it("returns the parsed JSON body on a 2xx response", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ hello: "world" }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const body = await request<{ hello: string }>("http://127.0.0.1:18080/api/thing");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/api/thing");
    expect(body).toEqual({ hello: "world" });
  });

  it("throws ApiError with code/message/issues extracted from a non-2xx {code,message,issues} body", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 400,
      json: async () => ({
        code: "contract_validation_failed",
        message: "候选数据不符合契约。",
        issues: [{ path: "defects[0].confidence", message: "must be between 0 and 1" }],
      }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const error = await request("http://127.0.0.1:18080/api/thing", { method: "PUT" }).catch(
      (caught: unknown) => caught
    );

    expect(error).toBeInstanceOf(ApiError);
    const apiError = error as ApiError;
    expect(apiError.code).toBe("contract_validation_failed");
    expect(apiError.message).toBe("候选数据不符合契约。");
    expect(apiError.issues).toEqual([{ path: "defects[0].confidence", message: "must be between 0 and 1" }]);
  });

  it("labels a code-less error body with the neutral synthetic code and stashes it on details", async () => {
    const codelessBody = { can_confirm: false, blocking_errors: [] };
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 409,
      json: async () => codelessBody,
    });
    vi.stubGlobal("fetch", fetchMock);

    const error = await request("http://127.0.0.1:18080/api/thing", { method: "POST" }).catch(
      (caught: unknown) => caught
    );

    expect(error).toBeInstanceOf(ApiError);
    const apiError = error as ApiError;
    expect(apiError.code).toBe("unrecognized_error_response");
    expect(apiError.details).toEqual(codelessBody);
  });

  it("uses the HTTP fallback when an error response contains a blank message", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 503,
      json: async () => ({ code: "database_unavailable", message: "   " }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const error = await request("http://127.0.0.1:18080/api/thing").catch((caught: unknown) => caught);

    expect(error).toBeInstanceOf(ApiError);
    expect((error as ApiError).message).toBe("请求失败（HTTP 503，错误码 database_unavailable）");
  });

  it("throws ApiError(invalid_response_body) when a 2xx body is not valid JSON", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => {
        throw new SyntaxError("Unexpected token < in JSON");
      },
    });
    vi.stubGlobal("fetch", fetchMock);

    const error = await request("http://127.0.0.1:18080/api/thing").catch((caught: unknown) => caught);

    expect(error).toBeInstanceOf(ApiError);
    expect((error as ApiError).code).toBe("invalid_response_body");
  });

  it("attaches the Authorization header when a session token is set", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({}),
    });
    vi.stubGlobal("fetch", fetchMock);
    setAuthToken("token-1");

    await request("http://127.0.0.1:18080/api/thing", { method: "POST" });

    const [, init] = fetchMock.mock.calls[0] as [string, RequestInit];
    expect(new Headers(init.headers).get("Authorization")).toBe("Bearer token-1");
  });

  it("fires the unauthorized handler on 401 only when the request carried a token", async () => {
    const handler = vi.fn();
    setUnauthorizedHandler(handler);
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 401,
      json: async () => ({ code: "auth_required", message: "请先登录。" }),
    });
    vi.stubGlobal("fetch", fetchMock);

    // 未带 token 的 401（例如登录接口密码错误）不触发全局登出。
    await request("http://127.0.0.1:18080/api/auth/login", { method: "POST" }).catch(() => undefined);
    expect(handler).not.toHaveBeenCalled();

    setAuthToken("token-1");
    await request("http://127.0.0.1:18080/api/thing").catch(() => undefined);
    expect(handler).toHaveBeenCalledTimes(1);
  });
});
