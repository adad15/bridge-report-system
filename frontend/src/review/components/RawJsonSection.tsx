import { Card, theme } from "antd";

import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";

interface RawJsonSectionProps {
  draft: BridgeAnnualInspectionData;
}

// "原始 JSON" 分组：只读展示当前草稿的完整 JSON，方便排查字段没有暴露在编辑表格里的情况。
export function RawJsonSection({ draft }: RawJsonSectionProps) {
  const { token } = theme.useToken();
  return (
    <Card title="原始 JSON">
      <pre
        style={{
          margin: 0,
          maxHeight: "60vh",
          overflow: "auto",
          padding: 12,
          borderRadius: token.borderRadius,
          background: token.colorFillQuaternary,
          fontSize: token.fontSizeSM,
          lineHeight: 1.6,
        }}
      >
        {JSON.stringify(draft, null, 2)}
      </pre>
    </Card>
  );
}
