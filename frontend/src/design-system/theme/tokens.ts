import type { ThemeConfig } from "antd";

/**
 * 桥梁检测报告系统的全局视觉基线。
 *
 * 业务页面应优先消费 Ant Design 派生后的语义 Token，不要复制这里的色值。
 * 只有经过设计规范确认的跨页面视觉决策才进入这个对象。
 */
export const bridgeReportTokens: NonNullable<ThemeConfig["token"]> = {
  colorPrimary: "#2F54EB",
  colorPrimaryHover: "#1D39C4",
  colorPrimaryBg: "#E8EDFF",
  colorInfo: "#2F54EB",
  colorInfoBg: "#E8EDFF",
  colorSuccess: "#1F7A55",
  colorSuccessBg: "#EAF7F0",
  colorWarning: "#B26A00",
  colorWarningBg: "#FFF4DD",
  colorError: "#C4323F",
  colorErrorBg: "#FDEDEF",
  colorLink: "#2F54EB",

  colorText: "#172033",
  colorTextSecondary: "#59677D",
  colorTextTertiary: "#8691A5",
  colorBgLayout: "#F1F4F9",
  colorBgContainer: "#FFFFFF",
  colorBgElevated: "#FFFFFF",
  colorBorder: "#D5DEEA",
  colorBorderSecondary: "#E3E8F0",

  fontFamily: '"Microsoft YaHei", "Segoe UI", system-ui, sans-serif',
  fontSize: 14,
  borderRadius: 8,
  borderRadiusLG: 12,
  controlHeight: 40,
  controlHeightLG: 44,
};
