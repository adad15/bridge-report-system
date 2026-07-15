import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from "react";

import { setAuthToken, setUnauthorizedHandler } from "../api/apiClient";
import { backendBaseUrl } from "../config";
import type { AuthUser } from "./authApi";
import { fetchCurrentUser, login as loginRequest, logout as logoutRequest } from "./authApi";

const TOKEN_STORAGE_KEY = "bridge_report_auth_token";

export interface AuthContextValue {
  /** 当前登录用户；null 表示未登录。 */
  user: AuthUser | null;
  /** 启动时正在用本地令牌恢复会话（期间不渲染登录页，避免闪烁）。 */
  restoring: boolean;
  login: (username: string, password: string) => Promise<void>;
  logout: () => Promise<void>;
}

const AuthContext = createContext<AuthContextValue | null>(null);

function readStoredToken(): string | null {
  try {
    return window.localStorage.getItem(TOKEN_STORAGE_KEY);
  } catch {
    return null;
  }
}

function writeStoredToken(token: string | null): void {
  try {
    if (token === null) {
      window.localStorage.removeItem(TOKEN_STORAGE_KEY);
    } else {
      window.localStorage.setItem(TOKEN_STORAGE_KEY, token);
    }
  } catch {
    // localStorage 不可用（隐私模式等）时会话仅存活于本次页面；不影响功能。
  }
}

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<AuthUser | null>(null);
  const [restoring, setRestoring] = useState(true);

  const clearSession = useCallback(() => {
    writeStoredToken(null);
    setAuthToken(null);
    setUser(null);
  }, []);

  // 启动恢复：本地有令牌就问一次 /api/auth/me；令牌失效则静默清除。
  useEffect(() => {
    setUnauthorizedHandler(clearSession);

    const stored = readStoredToken();
    if (stored === null) {
      setRestoring(false);
      return () => setUnauthorizedHandler(null);
    }

    setAuthToken(stored);
    let cancelled = false;
    fetchCurrentUser(backendBaseUrl)
      .then((result) => {
        if (!cancelled) setUser(result.user);
      })
      .catch(() => {
        // 401 已由 unauthorizedHandler 清除；其他错误（后端未启动等）也回到未登录态。
        if (!cancelled) clearSession();
      })
      .finally(() => {
        if (!cancelled) setRestoring(false);
      });

    return () => {
      cancelled = true;
      setUnauthorizedHandler(null);
    };
  }, [clearSession]);

  const login = useCallback(async (username: string, password: string) => {
    const result = await loginRequest(backendBaseUrl, username, password);
    writeStoredToken(result.token);
    setAuthToken(result.token);
    setUser(result.user);
  }, []);

  const logout = useCallback(async () => {
    try {
      await logoutRequest(backendBaseUrl);
    } catch {
      // 后端不可达也要完成本地登出。
    }
    clearSession();
  }, [clearSession]);

  const value = useMemo(
    () => ({ user, restoring, login, logout }),
    [user, restoring, login, logout]
  );

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth(): AuthContextValue {
  const value = useContext(AuthContext);
  if (value === null) {
    throw new Error("useAuth 必须在 AuthProvider 内使用。");
  }
  return value;
}
