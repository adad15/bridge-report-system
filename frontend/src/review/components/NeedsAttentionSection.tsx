import { useMemo, useState } from "react";

import type { BridgeAnnualInspectionData, PhotoCandidate, ReviewStatus } from "../../contracts/annualInspection";
import type { AttentionItem } from "../grouping";
import { formatAttentionItem } from "./displayHelpers";

interface NeedsAttentionSectionProps {
  items: AttentionItem[];
  draft: BridgeAnnualInspectionData;
  onSelect: (item: AttentionItem) => void;
}

// 大型报告的"需要处理"可能有上千行；每页最多渲染这么多，避免一次性挂载全部行。
const ATTENTION_PAGE_SIZE = 100;

// "需要处理" 分组（模块 05 §7.3）：类型 / 对象编号 / 问题说明 / 严重程度 / 处理状态。
// 行点击把该候选设为右侧证据面板的选中对象。
export function NeedsAttentionSection({ items, draft, onSelect }: NeedsAttentionSectionProps) {
  // 用 Map 把序号/状态查找降到 O(1)：否则每行都对 draft.defects/photos 做一次线性 find，
  // 上千行 × 上千候选会退化成 O(n²)，每次 draft 变动整屏重算导致卡顿。
  const defectIndexById = useMemo(() => {
    const map = new Map<string, number>();
    draft.defects.forEach((defect, index) => map.set(defect.candidate_id, index));
    return map;
  }, [draft.defects]);
  const defectStatusById = useMemo(() => {
    const map = new Map<string, ReviewStatus>();
    draft.defects.forEach((defect) => map.set(defect.candidate_id, defect.review_status));
    return map;
  }, [draft.defects]);
  const photoById = useMemo(() => {
    const map = new Map<string, PhotoCandidate>();
    draft.photos.forEach((photo) => map.set(photo.candidate_id, photo));
    return map;
  }, [draft.photos]);

  function objectLabel(item: AttentionItem): string {
    if (item.kind === "defect") {
      const index = defectIndexById.get(item.candidateId);
      return index === undefined ? "病害（目标已变化）" : `病害 ${index + 1}`;
    }
    if (item.kind === "photo") {
      const photo = photoById.get(item.candidateId);
      if (!photo) return "照片（目标已变化）";
      if (!photo.linked_defect_candidate_id) return `未关联照片 · ${photo.photo_number}`;
      const index = defectIndexById.get(photo.linked_defect_candidate_id);
      return index === undefined ? `照片 ${photo.photo_number}` : `病害 ${index + 1} · 照片 ${photo.photo_number}`;
    }
    if (item.kind === "rating") return "系统技术状况评定";
    return "导入记录";
  }

  // "处理状态" 列只对能在草稿里直接查到 review_status 的类型（defect/photo）有意义；
  // rating/import 级条目没有对应的候选对象可查，展示 "-"。
  function statusFor(item: AttentionItem): ReviewStatus | "-" {
    if (item.kind === "defect") return defectStatusById.get(item.candidateId) ?? "-";
    if (item.kind === "photo") return photoById.get(item.candidateId)?.review_status ?? "-";
    return "-";
  }

  if (items.length === 0) {
    return (
      <section className="status-panel">
        <h2>需要处理</h2>
        <p>暂无需要处理的候选。</p>
      </section>
    );
  }

  const errors = items.filter((item) => item.severity === "error");
  const warnings = items.filter((item) => item.severity !== "error");

  return (
    <section className="status-panel">
      <h2>需要处理</h2>
      <AttentionTable title="错误" items={errors} objectLabel={objectLabel} statusFor={statusFor} onSelect={onSelect} />
      <AttentionTable title="警告" items={warnings} objectLabel={objectLabel} statusFor={statusFor} onSelect={onSelect} />
    </section>
  );
}

function AttentionTable({
  title,
  items,
  objectLabel,
  statusFor,
  onSelect,
}: {
  title: string;
  items: AttentionItem[];
  objectLabel: (item: AttentionItem) => string;
  statusFor: (item: AttentionItem) => ReviewStatus | "-";
  onSelect: (item: AttentionItem) => void;
}) {
  const [page, setPage] = useState(0);
  if (items.length === 0) return null;
  const pageCount = Math.ceil(items.length / ATTENTION_PAGE_SIZE);
  const current = Math.min(page, pageCount - 1);
  const shown = items.slice(current * ATTENTION_PAGE_SIZE, (current + 1) * ATTENTION_PAGE_SIZE);
  return (
    <div className="attention-group">
      <h3>{title}</h3>
      <div className="table-scroll">
        <table className="data-table">
          <thead><tr><th>类型</th><th>对象编号</th><th>问题说明</th><th>严重程度</th><th>处理状态</th></tr></thead>
          <tbody>{shown.map((item, index) => (
            <tr key={`${item.kind}-${item.candidateId}-${current * ATTENTION_PAGE_SIZE + index}`} className={item.kind === "import" ? "" : "data-table-row-clickable"} title={formatAttentionItem(item)} onClick={() => item.kind !== "import" && onSelect(item)}>
              <td>{item.kind}</td><td>{objectLabel(item)}</td><td>{item.message}</td>
              <td><span className={`severity-badge severity-${item.severity}`}>{item.severity}</span></td>
              <td>{statusFor(item)}</td>
            </tr>
          ))}</tbody>
        </table>
      </div>
      {pageCount > 1 ? (
        <div className="attention-pagination">
          <button type="button" disabled={current === 0} onClick={() => setPage(current - 1)}>上一页</button>
          <span>第 {current + 1} / {pageCount} 页（共 {items.length} 条）</span>
          <button type="button" disabled={current + 1 >= pageCount} onClick={() => setPage(current + 1)}>下一页</button>
        </div>
      ) : null}
    </div>
  );
}
