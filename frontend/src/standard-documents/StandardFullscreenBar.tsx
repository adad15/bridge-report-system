import { FullscreenExitOutlined, ReadOutlined } from "@ant-design/icons";
import { Button, Flex, Typography, theme } from "antd";

import type { StandardDocument, StandardVersion } from "../api/standardDocumentsApi";
import { VersionStatusTag } from "./versionStatus";

interface Props {
  document: StandardDocument;
  version: StandardVersion;
  onExit: () => void;
}

/**
 * 全屏阅读时顶上那一条：读的是哪部规范、哪个版本，以及退出按钮（Esc 也能退出）。
 * 全屏里没有下拉之类的弹层：它们挂在 body 上，全屏元素之外的东西看不见。
 */
export function StandardFullscreenBar({ document, version, onExit }: Props) {
  const { token } = theme.useToken();
  return (
    <Flex
      align="center"
      justify="space-between"
      gap={16}
      style={{ flex: "none", height: 48, padding: `0 ${token.padding}px`, borderBottom: `1px solid ${token.colorBorderSecondary}` }}
    >
      <Flex align="center" gap={10} style={{ minWidth: 0 }}>
        <ReadOutlined style={{ color: token.colorPrimary }} />
        <Typography.Text strong ellipsis>{document.name}</Typography.Text>
        <Typography.Text type="secondary" style={{ whiteSpace: "nowrap" }}>{version.label} · {version.code}</Typography.Text>
        <VersionStatusTag status={version.status} />
      </Flex>
      <Flex align="center" gap={12} style={{ flex: "none" }}>
        <Typography.Text type="secondary">按 Esc 退出</Typography.Text>
        <Button icon={<FullscreenExitOutlined />} onClick={onExit}>退出全屏</Button>
      </Flex>
    </Flex>
  );
}
