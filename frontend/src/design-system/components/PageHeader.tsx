import { Divider, Flex, Grid, Typography } from "antd";
import type { ReactNode } from "react";

/**
 * 页面标题区：标题、一句说明、右侧页面级操作。
 *
 * 宽屏时说明跟在标题右边、中间一道竖线；窄屏时说明折到标题下面，操作按钮换行靠左。
 */
export function PageHeader({
  title,
  description,
  extra,
}: {
  title: ReactNode;
  description?: ReactNode;
  extra?: ReactNode;
}) {
  const screens = Grid.useBreakpoint();
  const wide = screens.xl !== false;

  return (
    <Flex component="header" align="end" justify="space-between" gap={16} wrap>
      <Flex align={wide ? "center" : "start"} vertical={!wide} gap={wide ? 18 : 4} style={{ minWidth: 0 }}>
        <Typography.Title level={3} style={{ margin: 0 }}>{title}</Typography.Title>
        {description ? (
          <>
            {wide ? <Divider vertical style={{ height: 32 }} /> : null}
            <Typography.Text type="secondary">{description}</Typography.Text>
          </>
        ) : null}
      </Flex>
      {extra ? <Flex align="center" gap={8} wrap>{extra}</Flex> : null}
    </Flex>
  );
}
