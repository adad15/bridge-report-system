import { App as AntdApp, ConfigProvider, theme } from "antd";
import zhCN from "antd/locale/zh_CN";
import dayjs from "dayjs";
import "dayjs/locale/zh-cn";
import { useLayoutEffect, type PropsWithChildren } from "react";

import { useCompactDensity } from "./density";
import { bridgeReportTheme } from "./theme";

// DatePicker 的月份名和"周一起排"取自 dayjs 的语言包，antd 的 zh_CN 只管按钮文案。
dayjs.locale("zh-cn");

/**
 * 文档根的字体、文字色与底色直接取主题 Token。
 *
 * 这三项原来写在 styles.css 的 :root 里，色值是另抄的一份，和 Token 差了一点点
 * （#18202a 对 #172033）。组件之外的裸文字、布局没盖住的边角都吃根上的值，
 * 所以要跟主题同源：改主题只改 tokens.ts 一处。
 */
function DocumentBaseStyle() {
  const { token } = theme.useToken();
  // 用 layout effect：首帧绘制前就写好，不闪一下浏览器默认的白底黑字。
  useLayoutEffect(() => {
    const root = document.documentElement.style;
    root.fontFamily = token.fontFamily;
    root.color = token.colorText;
    root.backgroundColor = token.colorBgLayout;
  }, [token.colorBgLayout, token.colorText, token.fontFamily]);
  return null;
}

/**
 * 全站唯一的 Ant Design 根级 Provider。
 *
 * ConfigProvider 统一主题与中文环境；App 提供 message、notification、modal
 * 等全局反馈能力对应的上下文。component=false 避免为了 Provider 改变现有 DOM 布局。
 */
export function DesignSystemProvider({ children }: PropsWithChildren) {
  // 窗口变窄或变矮时整站切紧凑档：控件、间距、字号和外壳尺寸一起收一档。
  const compact = useCompactDensity();
  return (
    <ConfigProvider locale={zhCN} theme={bridgeReportTheme(compact)}>
      <DocumentBaseStyle />
      <AntdApp component={false}>{children}</AntdApp>
    </ConfigProvider>
  );
}
