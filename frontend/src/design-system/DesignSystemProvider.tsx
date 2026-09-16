import { App as AntdApp, ConfigProvider } from "antd";
import zhCN from "antd/locale/zh_CN";
import dayjs from "dayjs";
import "dayjs/locale/zh-cn";
import type { PropsWithChildren } from "react";

import { bridgeReportTheme } from "./theme";

// DatePicker 的月份名和"周一起排"取自 dayjs 的语言包，antd 的 zh_CN 只管按钮文案。
dayjs.locale("zh-cn");

/**
 * 全站唯一的 Ant Design 根级 Provider。
 *
 * ConfigProvider 统一主题与中文环境；App 提供 message、notification、modal
 * 等全局反馈能力对应的上下文。component=false 避免为了 Provider 改变现有 DOM 布局。
 */
export function DesignSystemProvider({ children }: PropsWithChildren) {
  return (
    <ConfigProvider locale={zhCN} theme={bridgeReportTheme}>
      <AntdApp component={false}>{children}</AntdApp>
    </ConfigProvider>
  );
}
