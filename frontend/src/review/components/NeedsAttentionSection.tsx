import type { BridgeAnnualInspectionData, ReviewStatus } from "../../contracts/annualInspection";
import type { AttentionItem } from "../grouping";
import { formatAttentionItem } from "./displayHelpers";

interface NeedsAttentionSectionProps {
  items: AttentionItem[];
  draft: BridgeAnnualInspectionData;
  onSelect: (kind: AttentionItem["kind"], candidateId: string) => void;
}

// "处理状态" 列只对能在草稿里直接查到 review_status 的类型（defect/photo）有意义；
// rating/import 级条目没有对应的候选对象可查，展示 "-"。
function statusFor(item: AttentionItem, draft: BridgeAnnualInspectionData): ReviewStatus | "-" {
  if (item.kind === "defect") {
    return draft.defects.find((defect) => defect.candidate_id === item.candidateId)?.review_status ?? "-";
  }
  if (item.kind === "photo") {
    return draft.photos.find((photo) => photo.candidate_id === item.candidateId)?.review_status ?? "-";
  }
  return "-";
}

// "需要处理" 分组（模块 05 §7.3）：类型 / 对象编号 / 问题说明 / 严重程度 / 处理状态。
// 行点击把该候选设为右侧证据面板的选中对象。
export function NeedsAttentionSection({ items, draft, onSelect }: NeedsAttentionSectionProps) {
  if (items.length === 0) {
    return (
      <section className="status-panel">
        <h2>需要处理</h2>
        <p>暂无需要处理的候选。</p>
      </section>
    );
  }

  return (
    <section className="status-panel">
      <h2>需要处理</h2>
      <div className="table-scroll">
        <table className="data-table">
          <thead>
            <tr>
              <th>类型</th>
              <th>对象编号</th>
              <th>问题说明</th>
              <th>严重程度</th>
              <th>处理状态</th>
            </tr>
          </thead>
          <tbody>
            {items.map((item, index) => (
              <tr
                key={`${item.kind}-${item.candidateId}-${index}`}
                className="data-table-row-clickable"
                title={formatAttentionItem(item)}
                onClick={() => onSelect(item.kind, item.candidateId)}
              >
                <td>{item.kind}</td>
                <td>{item.candidateId}</td>
                <td>{item.message}</td>
                <td>
                  <span className={`severity-badge severity-${item.severity}`}>{item.severity}</span>
                </td>
                <td>{statusFor(item, draft)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}
