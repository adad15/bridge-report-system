import { useEffect, useState, type Dispatch } from "react";

import { photoContentUrl } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import type { ReviewDraftAction } from "../reviewDraft";
import { reviewTargetId } from "../reviewNavigation";

interface UnlinkedPhotosPanelProps {
  draft: BridgeAnnualInspectionData;
  importRecordId: string;
  baseUrl: string;
  selectedPhotoCandidateId?: string | null;
  dispatch: Dispatch<ReviewDraftAction>;
  disabled?: boolean;
}

export function UnlinkedPhotosPanel({ draft, importRecordId, baseUrl, selectedPhotoCandidateId, dispatch, disabled = false }: UnlinkedPhotosPanelProps) {
  const photos = draft.photos.filter((photo) => !photo.linked_defect_candidate_id);
  const [activeId, setActiveId] = useState(selectedPhotoCandidateId ?? photos[0]?.candidate_id ?? null);
  const active = photos.find((photo) => photo.candidate_id === activeId) ?? photos[0] ?? null;
  const [targetDefectId, setTargetDefectId] = useState(draft.defects[0]?.candidate_id ?? "");

  useEffect(() => {
    if (selectedPhotoCandidateId && photos.some((photo) => photo.candidate_id === selectedPhotoCandidateId)) setActiveId(selectedPhotoCandidateId);
  }, [photos, selectedPhotoCandidateId]);

  useEffect(() => {
    if (!draft.defects.some((defect) => defect.candidate_id === targetDefectId)) {
      setTargetDefectId(draft.defects[0]?.candidate_id ?? "");
    }
  }, [draft.defects, targetDefectId]);

  if (photos.length === 0) return null;
  return (
    <div id={reviewTargetId("unlinked-photos", "panel")} tabIndex={-1} className="unlinked-photos-panel">
      <h3>待关联及已处理照片</h3>
      <p>这里保留尚未关联、已确认无关或已忽略的照片，便于重新检查和恢复。</p>
      <div className="defect-photo-review">
        <div className="defect-photo-stage">{active ? <img className="defect-photo-stage-image active" src={photoContentUrl(baseUrl, importRecordId, active.candidate_id)} alt={`照片 ${active.photo_number}`} /> : null}</div>
        {active ? <div className="defect-photo-meta"><strong>照片 {active.photo_number}</strong><span>{active.extracted_file.original_caption ?? "无照片说明"}</span><span>匹配：{active.match_status} · 校对：{active.review_status}</span><label>目标病害<select disabled={disabled} aria-label="目标病害" value={targetDefectId} onChange={(event) => setTargetDefectId(event.target.value)}>{draft.defects.map((defect) => <option key={defect.candidate_id} value={defect.candidate_id}>{defect.component_name} / {defect.defect_location} / {defect.defect_type}</option>)}</select></label><div className="review-photo-actions"><button disabled={disabled || !targetDefectId} type="button" onClick={() => dispatch({ type: "photo_relink", candidateId: active.candidate_id, defectCandidateId: targetDefectId })}>关联到病害</button><button disabled={disabled} type="button" onClick={() => dispatch({ type: "photo_reset", candidateId: active.candidate_id })}>重置校对状态</button></div></div> : null}
        <div className="defect-photo-thumbnails">{photos.map((photo) => <button id={reviewTargetId("unlinked-photo", photo.candidate_id)} key={photo.candidate_id} type="button" className={photo.candidate_id === active?.candidate_id ? "active" : ""} aria-label={`查看待处理照片 ${photo.photo_number}`} onClick={() => setActiveId(photo.candidate_id)}><img src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)} alt="" /><span>{photo.photo_number}</span></button>)}</div>
      </div>
    </div>
  );
}
