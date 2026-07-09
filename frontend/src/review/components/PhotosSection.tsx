import type { Dispatch } from "react";

import type { DefectCandidate, PhotoCandidate } from "../../contracts/annualInspection";
import type { ReviewDraftAction } from "../reviewDraft";

interface PhotosSectionProps {
  photos: PhotoCandidate[];
  defects: DefectCandidate[];
  selectedCandidateId: string | null;
  onSelect: (candidateId: string) => void;
  dispatch: Dispatch<ReviewDraftAction>;
}

function defectOptionLabel(defect: DefectCandidate): string {
  return `${defect.component_name || "未命名构件"} · ${defect.defect_type || "未知类型"}（${defect.candidate_id}）`;
}

// "病害照片" 分组（模块 05 §7.3/§8.2）：照片编号可编辑，题注只读（extracted_file.original_caption
// 是 Word 原文题注，第一版不允许人工改），关联病害是下拉，匹配状态/校对状态只读展示——
// 它们只能通过下面四个操作按钮间接改变（reviewDraft.ts 没有直接写 match_status/review_status
// 任意值的 action，只有 photo_ignore 会把 review_status 设成已忽略）。
export function PhotosSection({ photos, defects, selectedCandidateId, onSelect, dispatch }: PhotosSectionProps) {
  if (photos.length === 0) {
    return (
      <section className="status-panel">
        <h2>病害照片</h2>
        <p>暂无照片候选。</p>
      </section>
    );
  }

  return (
    <section className="status-panel">
      <h2>病害照片</h2>
      <div className="table-scroll">
        <table className="data-table">
          <thead>
            <tr>
              <th>照片编号</th>
              <th>题注</th>
              <th>关联病害</th>
              <th>匹配状态</th>
              <th>校对状态</th>
              <th>操作</th>
            </tr>
          </thead>
          <tbody>
            {photos.map((photo) => (
              <tr
                key={photo.candidate_id}
                className={
                  photo.candidate_id === selectedCandidateId
                    ? "data-table-row-clickable data-table-row-selected"
                    : "data-table-row-clickable"
                }
                onClick={() => onSelect(photo.candidate_id)}
              >
                <td>
                  <input
                    type="text"
                    value={photo.photo_number}
                    onChange={(event) =>
                      dispatch({ type: "edit_photo_number", candidateId: photo.candidate_id, photoNumber: event.target.value })
                    }
                  />
                </td>
                <td>{photo.extracted_file.original_caption ?? "-"}</td>
                <td>
                  <select
                    value={photo.linked_defect_candidate_id ?? ""}
                    onChange={(event) =>
                      dispatch({
                        type: "edit_photo_link",
                        candidateId: photo.candidate_id,
                        defectCandidateId: event.target.value === "" ? null : event.target.value,
                      })
                    }
                  >
                    <option value="">未关联</option>
                    {defects.map((defect) => (
                      <option key={defect.candidate_id} value={defect.candidate_id}>
                        {defectOptionLabel(defect)}
                      </option>
                    ))}
                  </select>
                </td>
                <td>{photo.match_status}</td>
                <td>{photo.review_status}</td>
                <td className="review-photo-actions">
                  <button
                    type="button"
                    onClick={() => dispatch({ type: "photo_confirm_match", candidateId: photo.candidate_id })}
                  >
                    确认匹配
                  </button>
                  <button type="button" onClick={() => dispatch({ type: "photo_unlink", candidateId: photo.candidate_id })}>
                    取消匹配
                  </button>
                  <button
                    type="button"
                    onClick={() => dispatch({ type: "photo_mark_unrelated", candidateId: photo.candidate_id })}
                  >
                    标记未关联
                  </button>
                  <button type="button" onClick={() => dispatch({ type: "photo_ignore", candidateId: photo.candidate_id })}>
                    忽略
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}
