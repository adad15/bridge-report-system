import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";

interface RawJsonSectionProps {
  draft: BridgeAnnualInspectionData;
}

// "原始 JSON" 分组：只读展示当前草稿的完整 JSON，方便排查字段没有暴露在编辑表格里的情况。
export function RawJsonSection({ draft }: RawJsonSectionProps) {
  return (
    <section className="status-panel">
      <h2>原始 JSON</h2>
      <pre className="review-raw-json">{JSON.stringify(draft, null, 2)}</pre>
    </section>
  );
}
