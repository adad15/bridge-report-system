import { useEffect, useState, type Dispatch, type MouseEvent } from "react";

import { photoContentUrl } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData, DefectCandidate, ReviewStatus, StructurePart } from "../../contracts/annualInspection";
import { buildDefectPhotoGroup, canConfirmDefectPhotoGroup } from "../defectPhotoGroups";
import type { ReviewDraftAction } from "../reviewDraft";

const STRUCTURE_PARTS: StructurePart[] = ["全桥", "上部结构", "下部结构", "桥面系", "其他"];
const REVIEW_STATUSES: ReviewStatus[] = ["待确认", "已确认", "已修改", "已忽略"];

interface DefectPhotoGroupProps {
  draft: BridgeAnnualInspectionData;
  defect: DefectCandidate;
  importRecordId: string;
  baseUrl: string;
  expanded: boolean;
  onToggle: () => void;
  dispatch: Dispatch<ReviewDraftAction>;
  initialPhotoCandidateId?: string | null;
  disabled?: boolean;
}

function parsePhotoNumbers(text: string): string[] {
  return text.split(/[,，\s]+/).map((value) => value.trim()).filter(Boolean);
}

function keepRowOpen(event: MouseEvent<HTMLElement>): void {
  event.stopPropagation();
}

export function DefectPhotoGroup({ draft, defect, importRecordId, baseUrl, expanded, onToggle, dispatch, initialPhotoCandidateId, disabled = false }: DefectPhotoGroupProps) {
  const group = buildDefectPhotoGroup(draft, defect.candidate_id);
  const photos = group?.photos ?? [];
  const [activePhotoId, setActivePhotoId] = useState(initialPhotoCandidateId ?? photos[0]?.candidate_id ?? null);
  const activePhoto = photos.find((photo) => photo.candidate_id === activePhotoId) ?? photos[0] ?? null;
  const confirmation = canConfirmDefectPhotoGroup(draft, defect.candidate_id);

  useEffect(() => {
    if (!photos.some((photo) => photo.candidate_id === activePhotoId)) {
      setActivePhotoId(photos[0]?.candidate_id ?? null);
    }
  }, [activePhotoId, photos]);

  useEffect(() => {
    if (initialPhotoCandidateId && photos.some((photo) => photo.candidate_id === initialPhotoCandidateId)) setActivePhotoId(initialPhotoCandidateId);
  }, [initialPhotoCandidateId, photos]);

  const groupClassName = [
    "defect-photo-group",
    expanded ? "expanded" : "",
    defect.group_review_status === "已确认" ? "confirmed" : "pending",
    disabled ? "controls-disabled" : "",
  ].filter(Boolean).join(" ");

  return (
    <tbody className={groupClassName} aria-disabled={disabled}>
      <tr className={expanded ? "defect-summary-row data-table-row-selected" : "defect-summary-row"}>
        <td><select aria-label="结构部位" value={defect.structure_part} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "structure_part", value: event.target.value as StructurePart })}>{STRUCTURE_PARTS.map((part) => <option key={part}>{part}</option>)}</select></td>
        <td><input aria-label="构件类别" value={defect.component_name} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "component_name", value: event.target.value })} /></td>
        <td><input aria-label="构件编号" value={defect.component_alias ?? ""} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "component_alias", value: event.target.value || null })} /></td>
        <td><input aria-label="位置" value={defect.defect_location} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_location", value: event.target.value })} /></td>
        <td><input aria-label="病害类型" value={defect.defect_type} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_type", value: event.target.value })} /></td>
        <td><select aria-label="校对状态" value={defect.review_status} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "review_status", value: event.target.value as ReviewStatus })}>{REVIEW_STATUSES.map((status) => <option key={status}>{status}</option>)}</select></td>
      </tr>
      <tr className="defect-fact-detail-row">
        <td><label>数量<input aria-label="数量" value={defect.quantity_text ?? ""} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "quantity_text", value: event.target.value })} /></label></td>
        <td colSpan={3}><label>尺寸原文<input aria-label="尺寸原文" title={defect.measurement_text ?? ""} value={defect.measurement_text ?? ""} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_measurement_text", candidateId: defect.candidate_id, text: event.target.value })} /></label></td>
        <td colSpan={2}>
          <div className="defect-photo-number-control">
            <label>照片编号<input aria-label="照片编号" value={defect.photo_numbers.join(", ")} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "photo_numbers", value: parsePhotoNumbers(event.target.value) })} /></label>
            <button type="button" aria-expanded={expanded} onClick={onToggle}>{expanded ? "收起照片" : `查看照片（${photos.length}）`}</button>
          </div>
        </td>
      </tr>
      {expanded ? (
        <tr className="defect-photo-detail-row">
          <td colSpan={6}>
            <div className="defect-photo-review">
              <label className="review-note-field">校对备注<input aria-label="备注" value={defect.review_note ?? ""} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "review_note", value: event.target.value })} /></label>
              <div className="defect-photo-stage">
                {activePhoto ? <img className="defect-photo-stage-image active" src={photoContentUrl(baseUrl, importRecordId, activePhoto.candidate_id)} alt={`照片 ${activePhoto.photo_number}`} /> : <p>暂无已关联照片。</p>}
              </div>
              {activePhoto ? (
                <div className="defect-photo-meta">
                  <strong>照片 {activePhoto.photo_number}</strong>
                  <span>{activePhoto.extracted_file.original_caption ?? "无照片说明"}</span>
                  <span>匹配：{activePhoto.match_status} · 校对：{activePhoto.review_status}</span>
                  <div className="review-photo-actions">
                    <button type="button" onClick={() => dispatch({ type: "photo_confirm_match", candidateId: activePhoto.candidate_id })}>照片正确</button>
                    <button type="button" onClick={() => dispatch({ type: "photo_reset", candidateId: activePhoto.candidate_id })}>重置</button>
                    <button type="button" onClick={() => dispatch({ type: "photo_mark_unrelated", candidateId: activePhoto.candidate_id, note: "人工确认与病害无关" })}>确认无关</button>
                    <button type="button" onClick={() => dispatch({ type: "photo_ignore", candidateId: activePhoto.candidate_id })}>忽略</button>
                    <label>重新关联<select value={activePhoto.linked_defect_candidate_id ?? ""} onChange={(event) => event.target.value && dispatch({ type: "photo_relink", candidateId: activePhoto.candidate_id, defectCandidateId: event.target.value })}>{draft.defects.map((item) => <option key={item.candidate_id} value={item.candidate_id}>{item.component_name} / {item.defect_location} / {item.defect_type}</option>)}</select></label>
                  </div>
                </div>
              ) : null}
              {photos.length > 0 ? <div className="defect-photo-thumbnails">{photos.map((photo) => <button key={photo.candidate_id} type="button" className={photo.candidate_id === activePhoto?.candidate_id ? "active" : ""} aria-label={`查看照片 ${photo.photo_number}`} onClick={() => setActivePhotoId(photo.candidate_id)}><img src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)} alt="" /><span>{photo.photo_number}</span></button>)}</div> : null}
              {(group?.missingPhotoNumbers.length ?? 0) > 0 ? <div className="missing-photo-list"><strong>Word 中引用但未找到的照片</strong>{group?.missingPhotoNumbers.map((number) => { const confirmed = defect.confirmed_missing_photo_numbers.includes(number); return <div key={number}><span>照片 {number}</span><button type="button" onClick={() => dispatch({ type: confirmed ? "unconfirm_missing_photo" : "confirm_missing_photo", defectCandidateId: defect.candidate_id, photoNumber: number })}>{confirmed ? "撤销缺图确认" : "人工确认缺图"}</button></div>; })}</div> : null}
              <div className="defect-group-confirm"><span>{confirmation.ok ? "病害与照片均已具备确认条件。" : `尚不能确认：${confirmation.reasons.join("、")}`}</span><button type="button" disabled={!confirmation.ok} onClick={() => dispatch({ type: "confirm_defect_group", defectCandidateId: defect.candidate_id })}>确认本组</button></div>
            </div>
          </td>
        </tr>
      ) : null}
      <tr className="defect-group-spacer" aria-hidden="true"><td colSpan={6} /></tr>
    </tbody>
  );
}
