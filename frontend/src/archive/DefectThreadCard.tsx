import type { CSSProperties } from "react";

import type { ArchiveObservation, ArchiveThread } from "../api/componentArchiveApi";
import { categoryColor } from "../review/categoryColor";
import { ObservationYearRow } from "./ObservationYearRow";

interface DefectThreadCardProps {
  thread: ArchiveThread;
  componentType: string;
  onRebind?: (observation: ArchiveObservation) => void;
}

// A1 病害线索卡片（模块 06 §7.2）：病害是一级单位，年度观测在卡片内纵向排列。
// 标题展示"标准病害类型｜标准位置"两层位置语义中的线索层；年度实际位置在观测行展开后可见。
export function DefectThreadCard({ thread, componentType, onRebind }: DefectThreadCardProps) {
  return (
    <section
      className="archive-thread-card"
      style={{ "--defect-category-color": categoryColor(componentType) } as CSSProperties}
    >
      <header className="archive-thread-head">
        <strong>{thread.defect_type}</strong>
        <span>标准位置：{thread.defect_location ?? "未记录"}</span>
        <span className="archive-thread-span">
          {thread.first_seen_year !== null && thread.latest_seen_year !== null
            ? `${thread.first_seen_year} - ${thread.latest_seen_year}`
            : "暂无当前有效观测"}
        </span>
        <span className="archive-thread-number">{thread.system_number}</span>
      </header>
      {thread.observations.length > 0 ? (
        <div className="archive-thread-observations">
          {thread.observations.map((observation) => (
            <ObservationYearRow key={observation.id} observation={observation} onRebind={onRebind} />
          ))}
        </div>
      ) : (
        <p className="archive-empty-hint">该线索在当前有效版本中暂无观测记录。</p>
      )}
    </section>
  );
}
