// 轻量登录的三个端点。与其他领域 API 一样只走 apiClient.request；
// Authorization 头由 apiClient 的模块级 token 自动附加。

import { request } from "../api/apiClient";

const JSON_HEADERS = { "Content-Type": "application/json" };

export type UserRole = "admin" | "normal";

export interface AuthUser {
  username: string;
  display_name: string;
  role: UserRole;
}

export interface LoginResponse {
  token: string;
  user: AuthUser;
}

export async function login(baseUrl: string, username: string, password: string): Promise<LoginResponse> {
  return request<LoginResponse>(`${baseUrl}/api/auth/login`, {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ username, password }),
  });
}

export async function logout(baseUrl: string): Promise<{ logged_out: boolean }> {
  return request(`${baseUrl}/api/auth/logout`, { method: "POST" });
}

export async function fetchCurrentUser(baseUrl: string): Promise<{ user: AuthUser }> {
  return request(`${baseUrl}/api/auth/me`);
}
