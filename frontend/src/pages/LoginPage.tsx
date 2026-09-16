import {
  LockOutlined,
  SafetyCertificateOutlined,
  UserOutlined,
} from "@ant-design/icons";
import { Alert, Avatar, Button, Flex, Form, Grid, Input, Spin, Typography, theme } from "antd";
import { useState, type KeyboardEvent } from "react";

import { ApiError } from "../api/apiClient";
import { useAuth } from "../auth/AuthContext";
import { AuthLayout } from "../layouts/AuthLayout";

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
  const { token } = theme.useToken();
  const screens = Grid.useBreakpoint();

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
      <Flex
        component="section"
        vertical
        aria-labelledby="login-title"
        style={{ width: "100%", maxWidth: "clamp(380px, 32vw, 560px)" }}
      >
        <Flex vertical gap={6} style={{ marginBottom: "clamp(16px, 3vh, 30px)" }}>
          {/* 窄屏收掉了左侧品牌区，品牌就挪到表单上方。 */}
          {screens.md === false ? (
            <>
              <Avatar shape="square" size={44} icon={<BridgeMark />} style={{ backgroundColor: token.colorPrimary }} />
              <Typography.Text strong style={{ color: token.colorPrimary }}>桥梁检测报告系统</Typography.Text>
            </>
          ) : null}
          <Typography.Title level={2} id="login-title" style={{ margin: 0 }}>欢迎登录</Typography.Title>
          <Typography.Text type="secondary">登录桥梁检测报告编制平台</Typography.Text>
        </Flex>

        {error ? <Alert type="error" showIcon title={error} style={{ marginBottom: 16 }} /> : null}

        <Form<LoginFormValues>
          form={form}
          layout="vertical"
          requiredMark={false}
          initialValues={{ username: "", password: "" }}
          disabled={busy}
          onFinish={(values) => void handleSubmit(values)}
        >
          <Form.Item
            name="username"
            label="用户名"
            rules={[
              { required: true, message: "请输入用户名" },
              { whitespace: true, message: "用户名不能只包含空格" },
            ]}
          >
            <Input
              autoComplete="username"
              autoFocus
              prefix={<UserOutlined aria-hidden="true" />}
              placeholder="请输入用户名"
            />
          </Form.Item>

          <Form.Item
            name="password"
            label="密码"
            rules={[{ required: true, message: "请输入密码" }]}
            extra={
              capsLockOn ? (
                <Typography.Text type="warning">
                  <CapsLockIcon /> 大写锁定已开启
                </Typography.Text>
              ) : null
            }
          >
            <Input.Password
              autoComplete="current-password"
              prefix={<LockOutlined aria-hidden="true" />}
              placeholder="请输入密码"
              onKeyDown={trackCapsLock}
              onKeyUp={trackCapsLock}
            />
          </Form.Item>

          <Form.Item style={{ marginBottom: 0 }}>
            <Button
              type="primary"
              htmlType="submit"
              block
              autoInsertSpace={false}
              loading={busy}
              disabled={busy || username.trim() === "" || password === ""}
            >
              {busy ? "登录中…" : "登录"}
            </Button>
          </Form.Item>
        </Form>

        <Flex justify="center" style={{ marginTop: 13 }}>
          <Typography.Text type="secondary">
            <SafetyCertificateOutlined aria-hidden="true" /> 仅限获得授权的工作人员使用
          </Typography.Text>
        </Flex>
      </Flex>
    </AuthLayout>
  );
}

export function LoginRestoringPage() {
  return (
    <AuthLayout>
      <Flex vertical align="center" gap={18} aria-live="polite">
        <Spin size="large" />
        <Typography.Text type="secondary">正在恢复登录会话…</Typography.Text>
      </Flex>
    </AuthLayout>
  );
}

function BridgeMark() {
  return (
    <svg viewBox="0 0 32 32" width={27} height={27} aria-hidden="true">
      <path
        d="M4 20h24M4 20v7M28 20v7M4 20C7.6 8.7 24.4 8.7 28 20M11 13.4V20M16 11.6V20M21 13.4V20"
        fill="none"
        stroke="currentColor"
        strokeLinecap="round"
        strokeWidth={1.6}
      />
    </svg>
  );
}

function CapsLockIcon() {
  return (
    <svg viewBox="0 0 16 16" width={13} height={13} aria-hidden="true" style={{ verticalAlign: "-2px" }}>
      <path
        d="M8 2.5 3.1 7.8h2.6v3h4.6v-3h2.6L8 2.5ZM5.7 13.4h4.6"
        fill="none"
        stroke="currentColor"
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeWidth={1.4}
      />
    </svg>
  );
}
