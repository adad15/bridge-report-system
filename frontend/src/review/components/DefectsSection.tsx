import type { Dispatch } from "react";

import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import type { ReviewDraftAction } from "../reviewDraft";
import { DefectPhotoGroup } from "./DefectPhotoGroup";
import { UnlinkedPhotosPanel } from "./UnlinkedPhotosPanel";

interface DefectsSectionProps {
  draft: BridgeAnnualInspectionData;
  importRecordId: string;
  baseUrl: string;
  selectedCandidateId: string | null;
  onSelect: (candidateId: string) => void;
  dispatch: Dispatch<ReviewDraftAction>;
  selectedPhotoCandidateId?: string | null;
  disabled?: boolean;
}

export function DefectsSection({ draft, importRecordId, baseUrl, selectedCandidateId, selectedPhotoCandidateId, onSelect, dispatch, disabled = false }: DefectsSectionProps) {
  if (draft.defects.length === 0) return <section className="status-panel"><h2>病害与照片</h2><p>暂无病害候选。</p></section>;

  return (
    <section className="status-panel defect-photo-section">
      <h2>病害与照片</h2>
      <div className="table-scroll">
        <table className="data-table defect-photo-table">
          <thead><tr><th>结构部位</th><th>构件</th><th>位置</th><th>病害类型</th><th>数量</th><th>尺寸原文</th><th>照片编号</th><th>校对状态</th><th>备注</th></tr></thead>
          {draft.defects.map((defect) => <DefectPhotoGroup key={defect.candidate_id} draft={draft} defect={defect} importRecordId={importRecordId} baseUrl={baseUrl} expanded={selectedCandidateId === defect.candidate_id} initialPhotoCandidateId={selectedPhotoCandidateId} onToggle={() => onSelect(defect.candidate_id)} dispatch={dispatch} disabled={disabled} />)}
        </table>
      </div>
      <UnlinkedPhotosPanel draft={draft} importRecordId={importRecordId} baseUrl={baseUrl} selectedPhotoCandidateId={selectedPhotoCandidateId} dispatch={dispatch} disabled={disabled} />
    </section>
  );
}
