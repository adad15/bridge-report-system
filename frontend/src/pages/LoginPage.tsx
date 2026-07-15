import { useState, type FormEvent } from "react";

import { ApiError } from "../api/apiClient";
import { useAuth } from "../auth/AuthContext";

// 登录页：未登录时由 AppShell 直接渲染（不占路由），登录成功后 AuthContext
// 的 user 变化会让 AppShell 自动切回正常页面，无需跳转逻辑。
export function LoginPage() {
  const { login } = useAuth();
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function handleSubmit(event: FormEvent<HTMLFormElement>): Promise<void> {
    event.preventDefault();
    if (busy) return;
    setBusy(true);
    setError(null);
    try {
      await login(username.trim(), password);
    } catch (caught) {
      setError(caught instanceof ApiError ? caught.message : "登录失败，请稍后重试。");
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className="status-panel login-panel">
      <h1>桥梁检测报告系统</h1>
      <p>请使用系统账号登录。管理员账号可解锁已确认记录的全部修改。</p>
      <form className="login-form" onSubmit={handleSubmit}>
        <label>
          用户名
          <input
            autoComplete="username"
            value={username}
            onChange={(event) => setUsername(event.target.value)}
            disabled={busy}
          />
        </label>
        <label>
          密码
          <input
            type="password"
            autoComplete="current-password"
            value={password}
            onChange={(event) => setPassword(event.target.value)}
            disabled={busy}
          />
        </label>
        {error ? <p className="error-text">{error}</p> : null}
        <button type="submit" className="review-action-primary" disabled={busy || username.trim() === "" || password === ""}>
          {busy ? "登录中…" : "登录"}
        </button>
      </form>
    </section>
  );
}
