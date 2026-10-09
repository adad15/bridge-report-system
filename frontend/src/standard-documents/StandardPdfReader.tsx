import { theme } from "antd";

import { type StandardDocument, type StandardVersion, standardVersionContentUrl } from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";

interface Props {
  document: StandardDocument;
  version: StandardVersion;
  page?: number;
  /** 两栏时撑满卡片（"100%"），叠放时给固定高度。 */
  height: string;
}

/**
 * 原文阅读区：直接交给浏览器自带的 PDF 阅读器（设计 §5.4）。
 *
 * 目录（书签）、翻页、缩放、查找都在阅读器自己的工具栏和侧栏里，本页不画。
 * 以版本 id 作 key：切版本就重建 iframe，不让阅读器沿用上一份文件的翻页位置。
 */
export function StandardPdfReader({ document, version, page, height }: Props) {
  const { token } = theme.useToken();
  return (
    <iframe
      key={version.id}
      title={`规范原文：${version.code} ${document.name}`}
      src={standardVersionContentUrl(backendBaseUrl, version, { page })}
      style={{
        display: "block",
        width: "100%",
        height,
        border: `1px solid ${token.colorBorderSecondary}`,
        borderRadius: token.borderRadius,
        background: token.colorBgLayout,
      }}
    />
  );
}
