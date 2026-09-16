import type { ThemeConfig } from "antd";

import { bridgeReportTokens } from "./tokens";

/**
 * 根级 ConfigProvider 使用的主题配置。
 * 后续组件级 Token 统一在这里的 components 字段扩展。
 */
export const bridgeReportTheme: ThemeConfig = {
  token: bridgeReportTokens,
  components: {
    Form: {
      labelFontSize: 12,
      labelHeight: 14,
      verticalLabelPadding: "0 0 2px",
    },
    // 应用外壳：白色顶栏和侧栏压在浅灰底色上。顶栏高度和 AppLayout 里的 HEADER_HEIGHT 一致。
    Layout: {
      headerBg: bridgeReportTokens.colorBgContainer,
      headerHeight: 68,
      headerPadding: "0 24px",
      headerColor: bridgeReportTokens.colorText,
      siderBg: bridgeReportTokens.colorBgContainer,
      lightSiderBg: bridgeReportTokens.colorBgContainer,
      bodyBg: bridgeReportTokens.colorBgLayout,
    },
    // 侧栏导航用主色浅底标出当前项；顶栏的桥梁页签用主色下划线，贴着顶栏底边。
    Menu: {
      itemHeight: 44,
      itemBorderRadius: 9,
      itemMarginInline: 14,
      itemColor: bridgeReportTokens.colorTextSecondary,
      itemHoverColor: bridgeReportTokens.colorPrimary,
      itemHoverBg: "#F5F7FF",
      itemSelectedColor: bridgeReportTokens.colorPrimary,
      itemSelectedBg: bridgeReportTokens.colorPrimaryBg,
      groupTitleColor: bridgeReportTokens.colorTextTertiary,
      groupTitleFontSize: 12,
      iconSize: 17,
      collapsedIconSize: 17,
      activeBarBorderWidth: 0,
      activeBarHeight: 3,
      horizontalItemSelectedColor: bridgeReportTokens.colorPrimary,
      horizontalItemHoverColor: bridgeReportTokens.colorPrimary,
      horizontalLineHeight: "67px",
      itemBg: "transparent",
    },
  },
};

export { bridgeReportTokens } from "./tokens";
