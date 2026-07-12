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
      <fieldset className="review-disabled-fieldset" disabled={disabled}>
        <div className="table-scroll">
          {/* 每条病害是一张自带标签的表单卡片（DefectPhotoGroup），不再需要共享表头。 */}
          <table className="data-table defect-photo-table">
            {draft.defects.map((defect) => <DefectPhotoGroup key={defect.candidate_id} draft={draft} defect={defect} importRecordId={importRecordId} baseUrl={baseUrl} expanded={selectedCandidateId === defect.candidate_id} initialPhotoCandidateId={selectedPhotoCandidateId} onToggle={() => onSelect(defect.candidate_id)} dispatch={dispatch} disabled={disabled} />)}
          </table>
        </div>
        <UnlinkedPhotosPanel draft={draft} importRecordId={importRecordId} baseUrl={baseUrl} selectedPhotoCandidateId={selectedPhotoCandidateId} dispatch={dispatch} disabled={disabled} />
      </fieldset>
    </section>
  );
}
