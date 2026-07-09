import type { Dispatch } from "react";

import type { DefectCandidate, ReviewStatus, StructurePart } from "../../contracts/annualInspection";
import type { ReviewDraftAction } from "../reviewDraft";

const STRUCTURE_PARTS: StructurePart[] = ["全桥", "上部结构", "下部结构", "桥面系", "其他"];
const REVIEW_STATUSES: ReviewStatus[] = ["待确认", "已确认", "已修改", "已忽略"];

interface DefectsSectionProps {
  defects: DefectCandidate[];
  selectedCandidateId: string | null;
  onSelect: (candidateId: string) => void;
  dispatch: Dispatch<ReviewDraftAction>;
}

// photo_numbers 编辑框允许用逗号/中文逗号/空白分隔多个编号，dispatch 前拆成 string[]。
function parsePhotoNumbers(text: string): string[] {
  return text
    .split(/[,，\s]+/)
    .map((value) => value.trim())
    .filter((value) => value.length > 0);
}

// "普通病害" 分组（模块 05 §7.3/§8.1）：展示全部病害候选（不只是"正常"的那部分——
// defect_count 统计口径本就是全部），可编辑核心字段。行点击设为选中候选。
export function DefectsSection({ defects, selectedCandidateId, onSelect, dispatch }: DefectsSectionProps) {
  if (defects.length === 0) {
    return (
      <section className="status-panel">
        <h2>普通病害</h2>
        <p>暂无病害候选。</p>
      </section>
    );
  }

  return (
    <section className="status-panel">
      <h2>普通病害</h2>
      <div className="table-scroll">
        <table className="data-table">
          <thead>
            <tr>
              <th>结构部位</th>
              <th>构件</th>
              <th>位置</th>
              <th>病害类型</th>
              <th>数量</th>
              <th>尺寸原文</th>
              <th>照片编号</th>
              <th>校对状态</th>
              <th>备注</th>
            </tr>
          </thead>
          <tbody>
            {defects.map((defect) => (
              <tr
                key={defect.candidate_id}
                className={
                  defect.candidate_id === selectedCandidateId
                    ? "data-table-row-clickable data-table-row-selected"
                    : "data-table-row-clickable"
                }
                onClick={() => onSelect(defect.candidate_id)}
              >
                <td>
                  <select
                    value={defect.structure_part}
                    onChange={(event) =>
                      dispatch({
                        type: "edit_defect_field",
                        candidateId: defect.candidate_id,
                        field: "structure_part",
                        value: event.target.value as StructurePart,
                      })
                    }
                  >
                    {STRUCTURE_PARTS.map((part) => (
                      <option key={part} value={part}>
                        {part}
                      </option>
                    ))}
                  </select>
                </td>
                <td>
                  <input
                    type="text"
                    value={defect.component_name}
                    onChange={(event) =>
                      dispatch({
                        type: "edit_defect_field",
                        candidateId: defect.candidate_id,
                        field: "component_name",
                        value: event.target.value,
                      })
                    }
                  />
                </td>
                <td>
                  <input
                    type="text"
                    value={defect.defect_location}
                    onChange={(event) =>
                      dispatch({
                        type: "edit_defect_field",
                        candidateId: defect.candidate_id,
                        field: "defect_location",
                        value: event.target.value,
                      })
                    }
                  />
                </td>
                <td>
                  <input
                    type="text"
                    value={defect.defect_type}
                    onChange={(event) =>
                      dispatch({
                        type: "edit_defect_field",
                        candidateId: defect.candidate_id,
                        field: "defect_type",
                        value: event.target.value,
                      })
                    }
                  />
                </td>
                <td>
                  <input
                    type="text"
                    value={defect.quantity_text ?? ""}
                    onChange={(event) =>
                      dispatch({
                        type: "edit_defect_field",
                        candidateId: defect.candidate_id,
                        field: "quantity_text",
                        value: event.target.value,
                      })
                    }
                  />
                </td>
                <td>
                  <input
                    type="text"
                    value={defect.measurement_text ?? ""}
                    onChange={(event) =>
                      dispatch({
                        type: "edit_measurement_text",
                        candidateId: defect.candidate_id,
                        text: event.target.value,
                      })
                    }
                  />
                </td>
                <td>
                  <input
                    type="text"
                    value={defect.photo_numbers.join(", ")}
                    onChange={(event) =>
                      dispatch({
                        type: "edit_defect_field",
                        candidateId: defect.candidate_id,
                        field: "photo_numbers",
                        value: parsePhotoNumbers(event.target.value),
                      })
                    }
                  />
                </td>
                <td>
                  <select
                    value={defect.review_status}
                    onChange={(event) =>
                      dispatch({
                        type: "edit_defect_field",
                        candidateId: defect.candidate_id,
                        field: "review_status",
                        value: event.target.value as ReviewStatus,
                      })
                    }
                  >
                    {REVIEW_STATUSES.map((status) => (
                      <option key={status} value={status}>
                        {status}
                      </option>
                    ))}
                  </select>
                </td>
                <td>
                  <input
                    type="text"
                    value={defect.review_note ?? ""}
                    onChange={(event) =>
                      dispatch({
                        type: "edit_defect_field",
                        candidateId: defect.candidate_id,
                        field: "review_note",
                        value: event.target.value,
                      })
                    }
                  />
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}
