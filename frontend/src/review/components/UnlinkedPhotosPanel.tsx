import { useEffect, useState } from "react";

import { photoContentUrl } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import { reviewTargetId } from "../reviewNavigation";

interface UnlinkedPhotosPanelProps {
  draft: BridgeAnnualInspectionData;
  importRecordId: string;
  baseUrl: string;
  selectedPhotoCandidateId?: string | null;
}

/**
 * 只读清单：告诉用户还有多少张图没有着落，可预览，但不带归属操作。
 * 把图挂到病害上只有一个入口——病害卡片里的「添加照片」，不再有第二套说法。
 */
export function UnlinkedPhotosPanel({
  draft,
  importRecordId,
  baseUrl,
  selectedPhotoCandidateId,
}: UnlinkedPhotosPanelProps) {
  const photos = draft.photos.filter((photo) => !photo.linked_defect_candidate_id);
  const [activeId, setActiveId] = useState(selectedPhotoCandidateId ?? photos[0]?.candidate_id ?? null);
  const active = photos.find((photo) => photo.candidate_id === activeId) ?? photos[0] ?? null;

  useEffect(() => {
    if (selectedPhotoCandidateId && photos.some((photo) => photo.candidate_id === selectedPhotoCandidateId)) {
      setActiveId(selectedPhotoCandidateId);
    }
  }, [photos, selectedPhotoCandidateId]);

  if (photos.length === 0) return null;
  return (
    <div id={reviewTargetId("unlinked-photos", "panel")} tabIndex={-1} className="unlinked-photos-panel">
      <h3>未归属的照片（{photos.length} 张）</h3>
      <p>这些照片还没有挂到任何病害上。要归属其中一张，请在对应病害里点「添加照片」。</p>
      <div className="defect-photo-review">
        <div className="defect-photo-stage">
          {active ? (
            <img
              className="defect-photo-stage-image active"
              src={photoContentUrl(baseUrl, importRecordId, active.candidate_id)}
              alt={`照片 ${active.photo_number}`}
            />
          ) : null}
        </div>
        {active ? (
          <div className="defect-photo-meta">
            <strong>照片 {active.photo_number}</strong>
            <span>{active.extracted_file.original_caption ?? "无照片说明"}</span>
          </div>
        ) : null}
        <div className="defect-photo-thumbnails">
          {photos.map((photo) => (
            <button
              id={reviewTargetId("unlinked-photo", photo.candidate_id)}
              key={photo.candidate_id}
              type="button"
              className={photo.candidate_id === active?.candidate_id ? "active" : ""}
              aria-label={`查看未归属照片 ${photo.photo_number}`}
              onClick={() => setActiveId(photo.candidate_id)}
            >
              <img src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)} alt="" />
              <span>{photo.photo_number}</span>
            </button>
          ))}
        </div>
      </div>
    </div>
  );
}
