import {
  FileDoneOutlined,
  FolderOutlined,
  LineChartOutlined,
} from "@ant-design/icons";
import { Avatar, Card, Col, Flex, Grid, Row, Typography, theme } from "antd";
import type { PropsWithChildren } from "react";

import bridgeHeroImage from "../assets/auth/bridge-hero.png";

const capabilities = [
  { icon: <FolderOutlined />, label: "桥梁档案统一管理" },
  { icon: <FileDoneOutlined />, label: "年度检测资料校对" },
  { icon: <LineChartOutlined />, label: "跨年度病害追踪" },
];

/*
 * 登录页是全系统唯一的品牌页（设计 §9）：左侧浅蓝到浅紫的渐变和桥梁线稿属于品牌视觉，
 * 不是组件样式，antd 里没有对应的组件属性，所以集中写在这里，只此一处。
 */
const BRAND_PANEL_INLINE = "clamp(38px, 4.5vw, 68px)";

/** 认证页面的根级布局。品牌信息与表单内容分离，后续认证流程可复用同一外壳。 */
export function AuthLayout({ children }: PropsWithChildren) {
  const { token } = theme.useToken();
  const screens = Grid.useBreakpoint();
  // 窄于 768px 收掉左侧品牌区；窄于 576px 连外层面板也不要，表单铺满屏幕。
  const showBrand = screens.md !== false;
  const fullBleed = screens.sm === false;

  return (
    <Flex
      component="main"
      align="center"
      justify="center"
      style={{
        minHeight: "100dvh",
        padding: fullBleed ? 0 : 24,
        background: fullBleed ? token.colorBgContainer : token.colorBgLayout,
      }}
    >
      <Card
        role="region"
        aria-label="桥梁检测报告系统认证"
        style={{
          width: fullBleed ? "100%" : showBrand ? "min(1200px, calc(100vw - 48px))" : "min(520px, 100%)",
          height: fullBleed ? undefined : "min(612px, calc(100dvh - 48px))",
          minHeight: fullBleed ? "100dvh" : 520,
          overflow: "hidden",
          borderRadius: fullBleed ? 0 : token.borderRadiusLG,
          borderWidth: fullBleed ? 0 : undefined,
          boxShadow: fullBleed ? "none" : token.boxShadowSecondary,
        }}
        styles={{ body: { padding: 0, height: "100%" } }}
      >
        <Row wrap={false} style={{ height: "100%" }}>
          {showBrand ? (
            <Col flex="58.3 1 0" style={{ minWidth: 0 }}>
              <BrandPanel />
            </Col>
          ) : null}
          <Col flex="41.7 1 0" style={{ minWidth: 320 }}>
            <Flex
              component="section"
              align="center"
              justify="center"
              style={{ height: "100%", padding: fullBleed ? "32px 24px" : "clamp(24px, 6vh, 56px) clamp(40px, 4vw, 72px)" }}
            >
              {children}
            </Flex>
          </Col>
        </Row>
      </Card>
    </Flex>
  );
}

function BrandPanel() {
  const { token } = theme.useToken();

  return (
    <Flex
      component="aside"
      vertical
      aria-label="产品介绍"
      style={{
        height: "100%",
        overflow: "hidden",
        padding: `clamp(34px, 6.5vh, 56px) ${BRAND_PANEL_INLINE} clamp(28px, 5vh, 40px)`,
        background: `linear-gradient(145deg, ${token.colorPrimaryBg} 0%, #EEE9FF 100%)`,
      }}
    >
      <Flex align="center" gap={14}>
        <BridgeLineMark color={token.colorPrimary} />
        <Flex vertical>
          <Typography.Title level={3} style={{ margin: 0 }}>桥梁检测报告系统</Typography.Title>
          {/* 英文字标是品牌图形的一部分：固定的小字号和宽字距。 */}
          <Typography.Text type="secondary" strong style={{ fontSize: 10, letterSpacing: "0.17em" }}>
            BRIDGE INSPECTION
          </Typography.Text>
        </Flex>
      </Flex>
      <Typography.Paragraph type="secondary" style={{ margin: "16px 0 0" }}>
        公路桥梁定期检查、病害校对与报告编制平台
      </Typography.Paragraph>

      <Flex
        align="center"
        justify="center"
        aria-hidden="true"
        style={{ flex: "1 1 auto", minHeight: 150, maxHeight: 300, margin: `0 calc(${BRAND_PANEL_INLINE} * -1)`, overflow: "hidden" }}
      >
        <svg viewBox="0 0 2172 724" preserveAspectRatio="xMidYMid meet" focusable="false" style={{ width: "110%", flex: "none" }}>
          <defs>
            <filter id="auth-bridge-background-removal" colorInterpolationFilters="sRGB">
              <feColorMatrix
                type="matrix"
                values="1 0 0 0 0
                        0 1 0 0 0
                        0 0 1 0 0
                       -3 -3 -3 0 8.82"
                result="isolated-bridge"
              />
              <feComponentTransfer in="isolated-bridge" result="enhanced-bridge">
                <feFuncR type="linear" slope="0.82" />
                <feFuncG type="linear" slope="0.84" />
                <feFuncB type="linear" slope="0.92" />
                <feFuncA type="linear" slope="1.6" intercept="-0.12" />
              </feComponentTransfer>
              <feMorphology in="enhanced-bridge" operator="dilate" radius="1" />
            </filter>
          </defs>
          <image href={bridgeHeroImage} width="2172" height="724" filter="url(#auth-bridge-background-removal)" />
        </svg>
      </Flex>

      <Row role="list" aria-label="平台能力">
        {capabilities.map((capability) => (
          <Col span={8} key={capability.label} role="listitem">
            <Flex vertical align="center" gap={8}>
              <Avatar
                shape="square"
                size={48}
                icon={capability.icon}
                style={{ color: token.colorPrimary, backgroundColor: token.colorBgContainer, boxShadow: token.boxShadowTertiary }}
              />
              <Typography.Text style={{ whiteSpace: "nowrap" }}>{capability.label}</Typography.Text>
            </Flex>
          </Col>
        ))}
      </Row>
    </Flex>
  );
}

function BridgeLineMark({ color }: { color: string }) {
  return (
    <svg viewBox="0 0 64 44" width={42} height={38} aria-hidden="true">
      <path
        d="M4 34h56M2 38h60M25 6v28M29 12v22M25 8 7 34M25 15 15 34M29 14l17 20M29 9l28 25M13 38v4M51 38v4"
        fill="none"
        stroke={color}
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeWidth={1.8}
      />
    </svg>
  );
}
