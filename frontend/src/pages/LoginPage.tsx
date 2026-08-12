import { useState, type FormEvent, type KeyboardEvent } from "react";

import { ApiError } from "../api/apiClient";
import { useAuth } from "../auth/AuthContext";

// 登录页：未登录时由 AppShell 直接渲染（不占路由），登录成功后 AuthContext
// 的 user 变化会让 AppShell 自动切回正常页面，无需跳转逻辑。
export function LoginPage() {
  const { login } = useAuth();
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [passwordVisible, setPasswordVisible] = useState(false);
  const [capsLockOn, setCapsLockOn] = useState(false);
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

  // 大写锁定是密码输错最常见的原因，而后端只会回一句"用户名或密码不正确"。
  // 在密码框旁边就地提示，省掉用户反复重试才想起来看键盘的那几轮。
  function trackCapsLock(event: KeyboardEvent<HTMLInputElement>): void {
    setCapsLockOn(event.getModifierState("CapsLock"));
  }

  return (
    <section className="status-panel login-panel">
      <div className="login-brand">
        <span className="login-brand-badge" aria-hidden="true">
          <BridgeMark />
        </span>
        <div>
          <h1>桥梁检测报告系统</h1>
          <p className="login-brand-sub">公路桥梁定期检查与报告编制平台</p>
        </div>
      </div>

      <form className="login-form" onSubmit={handleSubmit}>
        {error ? (
          <p className="login-error" role="alert">
            <AlertIcon />
            {error}
          </p>
        ) : null}

        <div className="login-field">
          <label htmlFor="login-username">用户名</label>
          <div className="login-input-wrap">
            <UserIcon />
            <input
              id="login-username"
              autoComplete="username"
              // 未登录时整个应用只有这一张表单，光标不落在这里也没有别处可去。
              autoFocus
              value={username}
              onChange={(event) => setUsername(event.target.value)}
              disabled={busy}
            />
          </div>
        </div>

        <div className="login-field">
          <label htmlFor="login-password">密码</label>
          <div className="login-input-wrap">
            <LockIcon />
            <input
              id="login-password"
              type={passwordVisible ? "text" : "password"}
              autoComplete="current-password"
              value={password}
              onChange={(event) => setPassword(event.target.value)}
              onKeyDown={trackCapsLock}
              onKeyUp={trackCapsLock}
              disabled={busy}
            />
            <button
              type="button"
              className="login-password-toggle"
              aria-label={passwordVisible ? "隐藏密码" : "显示密码"}
              aria-pressed={passwordVisible}
              onClick={() => setPasswordVisible((visible) => !visible)}
              disabled={busy}
            >
              {passwordVisible ? <EyeOffIcon /> : <EyeIcon />}
            </button>
          </div>
          {capsLockOn ? (
            <p className="login-caps-hint">
              <CapsIcon />
              大写锁定已开启
            </p>
          ) : null}
        </div>

        <button
          type="submit"
          className="login-submit"
          disabled={busy || username.trim() === "" || password === ""}
        >
          {busy ? "登录中…" : "登录"}
        </button>
      </form>

      <p className="login-foot">管理员账号可解锁已确认记录的全部修改</p>
    </section>
  );
}

// 项目没引图标库，这几个只在登录页用一次，直接写成内联 SVG，比为它装一个依赖划算。
// 统一 currentColor + 无填充，颜色跟着外层文字走。
function BridgeMark() {
  return (
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round">
      <path d="M2 15h20" />
      <path d="M2 15v6M22 15v6" />
      <path d="M2 15Q12 -1 22 15" />
      <path d="M8 8.3v6.7M12 7v8M16 8.3v6.7" />
    </svg>
  );
}

function UserIcon() {
  return (
    <svg className="login-field-icon" viewBox="0 0 16 16" width="15" height="15" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" aria-hidden="true">
      <circle cx="8" cy="5.4" r="2.7" />
      <path d="M2.9 13.4c0-2.5 2.3-4.1 5.1-4.1s5.1 1.6 5.1 4.1" />
    </svg>
  );
}

function LockIcon() {
  return (
    <svg className="login-field-icon" viewBox="0 0 16 16" width="15" height="15" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" aria-hidden="true">
      <rect x="3.2" y="7" width="9.6" height="6.6" rx="1.6" />
      <path d="M5.6 7V5.1a2.4 2.4 0 0 1 4.8 0V7" />
    </svg>
  );
}

function EyeIcon() {
  return (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" aria-hidden="true">
      <path d="M1.4 8s2.6-4.4 6.6-4.4S14.6 8 14.6 8s-2.6 4.4-6.6 4.4S1.4 8 1.4 8Z" />
      <circle cx="8" cy="8" r="1.9" />
    </svg>
  );
}

function EyeOffIcon() {
  return (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" aria-hidden="true">
      <path d="M1.4 8s2.6-4.4 6.6-4.4S14.6 8 14.6 8s-2.6 4.4-6.6 4.4S1.4 8 1.4 8Z" />
      <circle cx="8" cy="8" r="1.9" />
      <path d="M2.6 13.4 13.4 2.6" />
    </svg>
  );
}

function AlertIcon() {
  return (
    <svg viewBox="0 0 16 16" width="15" height="15" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" aria-hidden="true">
      <circle cx="8" cy="8" r="6.4" />
      <path d="M8 4.7v3.8M8 11.2h.01" />
    </svg>
  );
}

function CapsIcon() {
  return (
    <svg viewBox="0 0 16 16" width="13" height="13" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 2.6 3.2 7.8h2.6v3.1h4.4V7.8h2.6z" />
      <path d="M5.8 13.4h4.4" />
    </svg>
  );
}
