import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { setAuthToken, setUnauthorizedHandler } from "../api/apiClient";
import { AuthProvider } from "../auth/AuthContext";
import { DesignSystemProvider } from "../design-system";
import { emulateViewport } from "../test/antd";
import { LoginPage, LoginRestoringPage } from "./LoginPage";

function renderLoginPage() {
  return render(
    <DesignSystemProvider>
      <AuthProvider>
        <LoginPage />
      </AuthProvider>
    </DesignSystemProvider>
  );
}

describe("LoginPage", () => {
  beforeEach(() => {
    window.localStorage.clear();
    // 左侧品牌区只在宽屏出现。
    emulateViewport(1440);
  });

  afterEach(() => {
    vi.restoreAllMocks();
    setAuthToken(null);
    setUnauthorizedHandler(null);
    window.localStorage.clear();
  });

  it("logs in and persists the session token", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({
        token: "token-1",
        user: { username: "user", display_name: "普通用户", role: "normal" },
      }),
    });
    vi.stubGlobal("fetch", fetchMock);

    renderLoginPage();
    await userEvent.type(screen.getByLabelText("用户名"), "user");
    await userEvent.type(screen.getByLabelText("密码"), "user123");
    await userEvent.click(screen.getByRole("button", { name: "登录" }));

    await waitFor(() => {
      expect(window.localStorage.getItem("bridge_report_auth_token")).toBe("token-1");
    });
    const [url, init] = fetchMock.mock.calls[0] as [string, RequestInit];
    expect(url).toBe("http://127.0.0.1:18080/api/auth/login");
    expect(JSON.parse(init.body as string)).toEqual({ username: "user", password: "user123" });
  });

  it("shows the backend message on wrong credentials without triggering global logout", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 401,
      json: async () => ({ code: "invalid_credentials", message: "用户名或密码不正确。" }),
    });
    vi.stubGlobal("fetch", fetchMock);

    renderLoginPage();
    await userEvent.type(screen.getByLabelText("用户名"), "user");
    await userEvent.type(screen.getByLabelText("密码"), "bad");
    await userEvent.click(screen.getByRole("button", { name: "登录" }));

    expect(await screen.findByText("用户名或密码不正确。")).toBeInTheDocument();
    expect(window.localStorage.getItem("bridge_report_auth_token")).toBeNull();
  });

  it("keeps the submit button disabled until both fields are filled", () => {
    vi.stubGlobal("fetch", vi.fn());
    renderLoginPage();
    expect(screen.getByRole("button", { name: "登录" })).toBeDisabled();
  });

  it("renders the shared authentication layout and approved security copy", () => {
    vi.stubGlobal("fetch", vi.fn());
    renderLoginPage();

    expect(screen.getByText("桥梁档案统一管理")).toBeInTheDocument();
    expect(screen.getByText("年度检测资料校对")).toBeInTheDocument();
    expect(screen.getByText("跨年度病害追踪")).toBeInTheDocument();
    expect(screen.getByText("仅限获得授权的工作人员使用")).toBeInTheDocument();
  });

  it("uses the authentication layout while restoring a saved session", () => {
    render(
      <DesignSystemProvider>
        <LoginRestoringPage />
      </DesignSystemProvider>
    );

    expect(screen.getByText("正在恢复登录会话…")).toBeInTheDocument();
    expect(screen.getByLabelText("产品介绍")).toBeInTheDocument();
  });
});
