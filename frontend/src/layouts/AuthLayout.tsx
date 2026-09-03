import {
  FileDoneOutlined,
  FolderOutlined,
  LineChartOutlined,
} from "@ant-design/icons";
import type { CSSProperties, PropsWithChildren } from "react";

import bridgeHeroImage from "../assets/auth/bridge-hero.png";
import { bridgeReportTokens } from "../design-system/theme/tokens";
import "./AuthLayout.css";

type AuthLayoutStyle = CSSProperties & Record<string, string | number>;

const authLayoutStyle: AuthLayoutStyle = {
  "--auth-primary": bridgeReportTokens.colorPrimary ?? "#2F54EB",
  "--auth-primary-bg": bridgeReportTokens.colorPrimaryBg ?? "#E8EDFF",
  "--auth-text": bridgeReportTokens.colorText ?? "#172033",
  "--auth-text-secondary": bridgeReportTokens.colorTextSecondary ?? "#59677D",
  "--auth-text-tertiary": bridgeReportTokens.colorTextTertiary ?? "#8691A5",
  "--auth-bg-layout": bridgeReportTokens.colorBgLayout ?? "#F1F4F9",
  "--auth-bg-container": bridgeReportTokens.colorBgContainer ?? "#FFFFFF",
  "--auth-border": bridgeReportTokens.colorBorder ?? "#DCE3EC",
};

const capabilities = [
  { icon: <FolderOutlined />, label: "桥梁档案统一管理" },
  { icon: <FileDoneOutlined />, label: "年度检测资料校对" },
  { icon: <LineChartOutlined />, label: "跨年度病害追踪" },
];

/** 认证页面的根级布局。品牌信息与表单内容分离，后续认证流程可复用同一外壳。 */
export function AuthLayout({ children }: PropsWithChildren) {
  return (
    <main className="auth-layout" style={authLayoutStyle}>
      <section className="auth-shell" aria-label="桥梁检测报告系统认证">
        <aside className="auth-brand-panel" aria-label="产品介绍">
          <div className="auth-brand-heading">
            <span className="auth-brand-mark" aria-hidden="true">
              <BridgeMark />
            </span>
            <div>
              <h1 className="auth-product-name">桥梁检测报告系统</h1>
              <p className="auth-product-english">BRIDGE INSPECTION</p>
            </div>
          </div>
          <p className="auth-brand-description">
            公路桥梁定期检查、病害校对与报告编制平台
          </p>

          <div className="auth-bridge-illustration" aria-hidden="true">
            <svg
              className="auth-bridge-image"
              viewBox="0 0 2172 724"
              preserveAspectRatio="xMidYMid meet"
              focusable="false"
            >
              <defs>
                <filter
                  id="auth-bridge-background-removal"
                  colorInterpolationFilters="sRGB"
                >
                  <feColorMatrix
                    type="matrix"
                    values="1 0 0 0 0
                            0 1 0 0 0
                            0 0 1 0 0
                           -3 -3 -3 0 8.82"
                    result="isolated-bridge"
                  />
                  <feComponentTransfer
                    in="isolated-bridge"
                    result="enhanced-bridge"
                  >
                    <feFuncR type="linear" slope="0.82" />
                    <feFuncG type="linear" slope="0.84" />
                    <feFuncB type="linear" slope="0.92" />
                    <feFuncA type="linear" slope="1.6" intercept="-0.12" />
                  </feComponentTransfer>
                  <feMorphology
                    in="enhanced-bridge"
                    operator="dilate"
                    radius="1"
                  />
                </filter>
              </defs>
              <image
                href={bridgeHeroImage}
                width="2172"
                height="724"
                filter="url(#auth-bridge-background-removal)"
              />
            </svg>
          </div>

          <ul className="auth-capability-list" aria-label="平台能力">
            {capabilities.map((capability) => (
              <li key={capability.label}>
                <span aria-hidden="true">{capability.icon}</span>
                {capability.label}
              </li>
            ))}
          </ul>
        </aside>

        <section className="auth-form-panel">{children}</section>
      </section>
    </main>
  );
}

function BridgeMark() {
  return (
    <svg viewBox="0 0 64 44" role="img">
      <path d="M4 34h56M2 38h60M25 6v28M29 12v22M25 8 7 34M25 15 15 34M29 14l17 20M29 9l28 25M13 38v4M51 38v4" />
    </svg>
  );
}
