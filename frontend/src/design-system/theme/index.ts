import { theme as antdTheme, type ThemeConfig } from "antd";

import { shellMetrics } from "../density";
import { bridgeReportTokens } from "./tokens";

/**
 * 根级 ConfigProvider 使用的主题配置。
 *
 * 两档密度：宽松档给大屏，紧凑档给笔记本（触发条件见 `density.ts`）。紧凑档在 antd 的
 * `compactAlgorithm` 之上再压一档控件高度与字号——算法只收内边距，控件高度是我们自己
 * 在 Token 里拔高的，不一起收的话，输入框仍然是 40px。
 */
export function bridgeReportTheme(compact: boolean): ThemeConfig {
  const shell = shellMetrics(compact);
  const menuItemHeight = compact ? 38 : 44;
  return {
    algorithm: compact ? antdTheme.compactAlgorithm : undefined,
    token: {
      ...bridgeReportTokens,
      ...(compact
        ? { fontSize: 13, controlHeight: 32, controlHeightLG: 36, borderRadiusLG: 10 }
        : null),
    },
    components: {
      Form: {
        labelFontSize: 12,
        labelHeight: 14,
        verticalLabelPadding: "0 0 2px",
      },
      // 应用外壳：白色顶栏和侧栏压在浅灰底色上。尺寸取自 density.ts，AppLayout 用同一份。
      Layout: {
        headerBg: bridgeReportTokens.colorBgContainer,
        headerHeight: shell.headerHeight,
        headerPadding: compact ? "0 16px" : "0 24px",
        headerColor: bridgeReportTokens.colorText,
        siderBg: bridgeReportTokens.colorBgContainer,
        lightSiderBg: bridgeReportTokens.colorBgContainer,
        bodyBg: bridgeReportTokens.colorBgLayout,
      },
      // 侧栏导航用主色浅底标出当前项；顶栏的桥梁页签用主色下划线，贴着顶栏底边。
      Menu: {
        itemHeight: menuItemHeight,
        itemBorderRadius: 9,
        itemMarginInline: compact ? 10 : 14,
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
        // 顶栏页签的下划线贴着顶栏底边：行高比顶栏矮 1px。
        horizontalLineHeight: `${shell.headerHeight - 1}px`,
        itemBg: "transparent",
      },
    },
  };
}

export { bridgeReportTokens } from "./tokens";
