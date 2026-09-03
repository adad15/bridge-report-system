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
  },
};

export { bridgeReportTokens } from "./tokens";
