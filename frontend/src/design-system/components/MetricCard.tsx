import { Avatar, Card, Flex, Statistic, Typography, theme } from "antd";
import type { ReactNode } from "react";

export type MetricTone = "primary" | "success" | "warning" | "error" | "neutral";

/**
 * 指标卡：左边一块语义色图标，右边标题和数值。
 *
 * 同一排指标卡用 `Row` / `Col` 等分排开；语义色只表达业务含义（待办用警示色、
 * 已完成用成功色），不为了好看轮换颜色。
 */
export function MetricCard({
  title,
  value,
  icon,
  tone = "primary",
  suffix,
  description,
  size = "default",
}: {
  title: ReactNode;
  value: number | string;
  icon: ReactNode;
  tone?: MetricTone;
  suffix?: ReactNode;
  /** 数值下面一行灰字口径说明；同一排卡片要么都给、要么都不给，免得高低不齐。 */
  description?: ReactNode;
  /** small 用在要一屏放下的页面（桥梁概览）：卡片内边距和图标都收一档。 */
  size?: "default" | "small";
}) {
  const { token } = theme.useToken();
  const palette: Record<MetricTone, { color: string; background: string }> = {
    primary: { color: token.colorPrimary, background: token.colorPrimaryBg },
    success: { color: token.colorSuccess, background: token.colorSuccessBg },
    warning: { color: token.colorWarning, background: token.colorWarningBg },
    error: { color: token.colorError, background: token.colorErrorBg },
    neutral: { color: token.colorTextSecondary, background: token.colorFillTertiary },
  };
  const { color, background } = palette[tone];

  return (
    <Card size={size === "small" ? "small" : undefined}>
      <Flex align="center" gap={size === "small" ? 14 : 20}>
        <Avatar
          shape="square"
          size={size === "small" ? 42 : 56}
          icon={icon}
          style={{ flex: "none", color, backgroundColor: background, borderRadius: token.borderRadiusLG }}
        />
        <Flex vertical gap={2} style={{ minWidth: 0 }}>
          <Statistic title={title} value={value} suffix={suffix} />
          {description ? (
            <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>{description}</Typography.Text>
          ) : null}
        </Flex>
      </Flex>
    </Card>
  );
}
