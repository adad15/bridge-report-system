import type { ReactNode } from "react";

import type { BridgeAnnualInspectionData, WarningItem } from "../../contracts/annualInspection";
import type { AttentionItem } from "../grouping";

export interface SelectedCandidate {
  kind: AttentionItem["kind"];
  candidateId: string;
}

interface EvidencePanelProps {
  selected: SelectedCandidate | null;
  draft: BridgeAnnualInspectionData;
}

function WarningList({ warnings }: { warnings: WarningItem[] }) {
  if (warnings.length === 0) {
    return <p>无 warning。</p>;
  }
  return (
    <ul className="review-warning-list">
      {warnings.map((warning, index) => (
        <li key={index} className={warning.severity === "error" ? "error-text" : "warning-text"}>
          {warning.message}
        </li>
      ))}
    </ul>
  );
}

function Shell({ children }: { children: ReactNode }) {
  return (
    <aside className="status-panel evidence-panel">
      <h2>来源证据</h2>
      {children}
    </aside>
  );
}

// 右侧证据面板（模块 05 §7.4）：candidate_id、原始行文本、章节/表名/行号、置信度、
// warnings、题注（照片）——全部只读。病害/照片有完整证据字段；评分候选没有
// candidate_id/warnings（模块 03 契约本就没有这两个字段），评级/导入级条目只能退化展示
// candidate_id，不强行伪造它们没有的数据。
export function EvidencePanel({ selected, draft }: EvidencePanelProps) {
  if (selected === null) {
    return (
      <Shell>
        <p>请选择左侧候选查看来源证据。</p>
      </Shell>
    );
  }

  if (selected.kind === "defect") {
    const defect = draft.defects.find((item) => item.candidate_id === selected.candidateId);
    if (!defect) {
      return (
        <Shell>
          <p>未找到该候选（可能已被移除）：{selected.candidateId}</p>
        </Shell>
      );
    }
    return (
      <Shell>
        <div className="status-grid">
          <span>candidate_id</span>
          <span>{defect.candidate_id}</span>
          <span>章节</span>
          <span>{defect.source_ref.chapter ?? "-"}</span>
          <span>表名</span>
          <span>{defect.source_ref.table_title ?? "-"}</span>
          <span>行号</span>
          <span>{defect.source_ref.row_index ?? "-"}</span>
          <span>置信度</span>
          <span>{defect.confidence.toFixed(2)}</span>
        </div>
        <p className="evidence-raw-text">{defect.source_ref.raw_row_text ?? "（无原始行文本）"}</p>
        <WarningList warnings={defect.warnings} />
      </Shell>
    );
  }

  if (selected.kind === "photo") {
    const photo = draft.photos.find((item) => item.candidate_id === selected.candidateId);
    if (!photo) {
      return (
        <Shell>
          <p>未找到该候选（可能已被移除）：{selected.candidateId}</p>
        </Shell>
      );
    }
    return (
      <Shell>
        <div className="status-grid">
          <span>candidate_id</span>
          <span>{photo.candidate_id}</span>
          <span>章节</span>
          <span>{photo.source_ref.chapter ?? "-"}</span>
          <span>表名</span>
          <span>{photo.source_ref.table_title ?? "-"}</span>
          <span>行号</span>
          <span>{photo.source_ref.row_index ?? "-"}</span>
          <span>置信度</span>
          <span>{photo.confidence.toFixed(2)}</span>
          <span>题注</span>
          <span>{photo.extracted_file.original_caption ?? "-"}</span>
        </div>
        <p className="evidence-raw-text">{photo.source_ref.raw_row_text ?? "（无原始行文本）"}</p>
        <WarningList warnings={photo.warnings} />
      </Shell>
    );
  }

  return (
    <Shell>
      <p>candidate_id：{selected.candidateId}</p>
      <p>该类型候选暂不支持在此面板查看完整来源证据，请在对应分组内处理。</p>
    </Shell>
  );
}
