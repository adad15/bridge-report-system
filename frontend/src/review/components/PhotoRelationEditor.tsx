import { useEffect, useState, type Dispatch } from "react";

import { photoContentUrl } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData, DefectCandidate } from "../../contracts/annualInspection";
import type { ReviewDraftAction } from "../reviewDraft";

interface PhotoRelationEditorProps {
  draft: BridgeAnnualInspectionData;
  defect: DefectCandidate;
  importRecordId: string;
  baseUrl: string;
  initialPhotoCandidateId?: string | null;
  dispatch: Dispatch<ReviewDraftAction>;
  disabled?: boolean;
}

const RESOLUTION_LABELS = {
  pending: "待处理",
  matched: "已匹配本病害",
  relinked: "原文引用已改绑",
  missing: "已确认缺图",
  unrelated: "已确认无关",
} as const;

export function PhotoRelationEditor({
  draft,
  defect,
  importRecordId,
  baseUrl,
  initialPhotoCandidateId,
  dispatch,
  disabled = false,
}: PhotoRelationEditorProps) {
  const photos = draft.photos.filter(
    (photo) => photo.linked_defect_candidate_id === defect.candidate_id,
  );
  const [activePhotoId, setActivePhotoId] = useState(
    initialPhotoCandidateId ?? photos[0]?.candidate_id ?? null,
  );
  const activePhoto = photos.find((photo) => photo.candidate_id === activePhotoId) ?? photos[0] ?? null;

  useEffect(() => {
    if (initialPhotoCandidateId) setActivePhotoId(initialPhotoCandidateId);
  }, [initialPhotoCandidateId]);

  return (
    <section className="photo-relation-editor">
      <h4>照片关系与 Word 引用</h4>
      <div className="photo-relation-layout">
        <div className="photo-relation-stage">
          {activePhoto ? (
            <img
              src={photoContentUrl(baseUrl, importRecordId, activePhoto.candidate_id)}
              alt={`照片 ${activePhoto.photo_number}`}
            />
          ) : <p>当前病害没有已关联照片。</p>}
          <div className="photo-relation-thumbnails">
            {photos.map((photo) => (
              <button
                key={photo.candidate_id}
                type="button"
                className={photo.candidate_id === activePhoto?.candidate_id ? "active" : ""}
                onClick={() => setActivePhotoId(photo.candidate_id)}
              >
                <img loading="lazy" src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)} alt="" />
                <span>{photo.photo_number}</span>
              </button>
            ))}
          </div>
        </div>
        <div className="photo-reference-list">
          {defect.photo_references.length === 0 ? <p>Word 原文没有照片编号。</p> : null}
          {defect.photo_references.map((reference) => {
            const candidates = draft.photos.filter(
              (photo) => photo.photo_number === reference.photo_number,
            );
            return (
              <div key={reference.photo_number} className="photo-reference-item">
                <div>
                  <strong>照片 {reference.photo_number}</strong>
                  <span>{RESOLUTION_LABELS[reference.resolution]}</span>
                </div>
                {reference.resolution === "pending" ? (
                  <div className="photo-reference-actions">
                    {candidates.map((photo) => (
                      <span key={photo.candidate_id} className="photo-reference-candidate-actions">
                        <button
                          type="button"
                          disabled={disabled}
                          onClick={() => dispatch({
                            type: "confirm_photo_reference_match",
                            defectCandidateId: defect.candidate_id,
                            photoNumber: reference.photo_number,
                            photoCandidateId: photo.candidate_id,
                          })}
                        >
                          照片正确{candidates.length > 1 ? `（${photo.candidate_id}）` : ""}
                        </button>
                        {photo.linked_defect_candidate_id &&
                        photo.linked_defect_candidate_id !== defect.candidate_id ? (
                          <button
                            type="button"
                            disabled={disabled}
                            onClick={() => dispatch({
                              type: "relink_photo_reference",
                              defectCandidateId: defect.candidate_id,
                              photoNumber: reference.photo_number,
                              photoCandidateId: photo.candidate_id,
                              targetDefectCandidateId: photo.linked_defect_candidate_id!,
                            })}
                          >
                            原文引用到其他病害
                          </button>
                        ) : null}
                        {!photo.linked_defect_candidate_id ? (
                          <button
                            type="button"
                            disabled={disabled}
                            onClick={() => dispatch({
                              type: "confirm_unrelated_photo_reference",
                              defectCandidateId: defect.candidate_id,
                              photoNumber: reference.photo_number,
                              photoCandidateId: photo.candidate_id,
                            })}
                          >
                            确认无关
                          </button>
                        ) : null}
                      </span>
                    ))}
                    {candidates.length === 0 ? (
                      <button
                        type="button"
                        disabled={disabled}
                        onClick={() => dispatch({
                          type: "confirm_missing_photo",
                          defectCandidateId: defect.candidate_id,
                          photoNumber: reference.photo_number,
                        })}
                      >
                        确认缺图
                      </button>
                    ) : null}
                  </div>
                ) : (
                  <button
                    type="button"
                    disabled={disabled}
                    onClick={() => dispatch({
                      type: "reset_photo_reference_review",
                      defectCandidateId: defect.candidate_id,
                      photoNumber: reference.photo_number,
                    })}
                  >
                    撤销处理
                  </button>
                )}
              </div>
            );
          })}
        </div>
      </div>
    </section>
  );
}
