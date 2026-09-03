import {
  LockOutlined,
  SafetyCertificateOutlined,
  UserOutlined,
} from "@ant-design/icons";
import { Alert, Button, Form, Input, Spin } from "antd";
import { useState, type KeyboardEvent } from "react";

import { ApiError } from "../api/apiClient";
import { useAuth } from "../auth/AuthContext";
import { AuthLayout } from "../layouts/AuthLayout";
import "./LoginPage.css";

interface LoginFormValues {
  username: string;
  password: string;
}

// 登录页仍由 AuthContext 负责会话写入；此处只迁移视觉层与表单交互。
export function LoginPage() {
  const { login } = useAuth();
  const [form] = Form.useForm<LoginFormValues>();
  const username = Form.useWatch("username", form) ?? "";
  const password = Form.useWatch("password", form) ?? "";
  const [capsLockOn, setCapsLockOn] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function handleSubmit(values: LoginFormValues): Promise<void> {
    if (busy) return;
    setBusy(true);
    setError(null);
    try {
      await login(values.username.trim(), values.password);
    } catch (caught) {
      setError(caught instanceof ApiError ? caught.message : "登录失败，请稍后重试。");
    } finally {
      setBusy(false);
    }
  }

  // 后端不会区分密码错误与大小写锁定，提前给出键盘状态提示能减少无效重试。
  function trackCapsLock(event: KeyboardEvent<HTMLInputElement>): void {
    setCapsLockOn(event.getModifierState("CapsLock"));
  }

  return (
    <AuthLayout>
      <section className="login-content" aria-labelledby="login-title">
        <header className="login-heading">
          <span className="login-mobile-mark" aria-hidden="true">
            <BridgeMark />
          </span>
          <p className="login-eyebrow">桥梁检测报告系统</p>
          <h2 id="login-title">欢迎登录</h2>
          <p>登录桥梁检测报告编制平台</p>
        </header>

        {error ? (
          <Alert
            className="login-error-alert"
            type="error"
            showIcon
            title={error}
          />
        ) : null}

        <Form<LoginFormValues>
          className="login-form"
          form={form}
          layout="vertical"
          requiredMark={false}
          size="small"
          styles={{
            label: { height: 14, paddingBottom: 0, fontSize: 12, lineHeight: "14px" },
            content: { minHeight: 0 },
          }}
          initialValues={{ username: "", password: "" }}
          disabled={busy}
          onFinish={(values) => void handleSubmit(values)}
        >
          <Form.Item
            name="username"
            label={<span className="login-field-label">用户名</span>}
            rules={[
              { required: true, message: "请输入用户名" },
              { whitespace: true, message: "用户名不能只包含空格" },
            ]}
            style={{ marginBottom: 16 }}
          >
            <Input
              className="login-input-control"
              autoComplete="username"
              autoFocus
              prefix={<UserOutlined aria-hidden="true" />}
              placeholder="请输入用户名"
            />
          </Form.Item>

          <Form.Item
            name="password"
            label={<span className="login-field-label">密码</span>}
            rules={[{ required: true, message: "请输入密码" }]}
            extra={
              capsLockOn ? (
                <span className="login-caps-hint">
                  <CapsLockIcon />
                  大写锁定已开启
                </span>
              ) : null
            }
            style={{ marginBottom: 18 }}
          >
            <Input.Password
              className="login-input-control"
              autoComplete="current-password"
              prefix={<LockOutlined aria-hidden="true" />}
              placeholder="请输入密码"
              onKeyDown={trackCapsLock}
              onKeyUp={trackCapsLock}
            />
          </Form.Item>

          <Form.Item style={{ marginBottom: 0 }}>
            <Button
              className="login-submit-button"
              type="primary"
              htmlType="submit"
              size="small"
              block
              autoInsertSpace={false}
              loading={busy}
              disabled={busy || username.trim() === "" || password === ""}
            >
              {busy ? "登录中…" : "登录"}
            </Button>
          </Form.Item>
        </Form>

        <p className="login-security-note">
          <SafetyCertificateOutlined aria-hidden="true" />
          仅限获得授权的工作人员使用
        </p>
      </section>
    </AuthLayout>
  );
}

export function LoginRestoringPage() {
  return (
    <AuthLayout>
      <section className="login-loading-state" aria-live="polite">
        <Spin size="large" />
        <p>正在恢复登录会话…</p>
      </section>
    </AuthLayout>
  );
}

function BridgeMark() {
  return (
    <svg viewBox="0 0 32 32">
      <path d="M4 20h24M4 20v7M28 20v7M4 20C7.6 8.7 24.4 8.7 28 20M11 13.4V20M16 11.6V20M21 13.4V20" />
    </svg>
  );
}

function CapsLockIcon() {
  return (
    <svg viewBox="0 0 16 16" aria-hidden="true">
      <path d="M8 2.5 3.1 7.8h2.6v3h4.6v-3h2.6L8 2.5ZM5.7 13.4h4.6" />
    </svg>
  );
}
