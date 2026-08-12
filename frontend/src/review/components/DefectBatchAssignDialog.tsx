import type { RatingTreeNodeSummary } from "../../api/ratingTreeApi";
import { ratingTreeDisplayLabel } from "../../rating-tree/ratingTreeLabels";
import type { DefectIssueGroup } from "../defectIssueGroups";

interface DefectBatchAssignDialogProps {
  assignment: { group: DefectIssueGroup; node: RatingTreeNodeSummary } | null;
  onCancel: () => void;
  onConfirm: () => void;
}

export function DefectBatchAssignDialog({
  assignment,
  onCancel,
  onConfirm,
}: DefectBatchAssignDialogProps) {
  if (!assignment) return null;
  return (
    <div className="review-dialog-backdrop" role="presentation">
      <div className="review-dialog defect-batch-dialog" role="dialog" aria-modal="true" aria-labelledby="defect-batch-assign-title">
        <h3 id="defect-batch-assign-title">批量指定评定树病害</h3>
        <p>
          将同一来源身份下的 {assignment.group.rows.length} 条病害统一指定为
          “{ratingTreeDisplayLabel(assignment.node)}”。
        </p>
        <p>构件、位置、描述、照片和来源编号均保持原值。</p>
        <p>此操作只更新当前校对草稿，不会修改已发布的评定树版本。</p>
        <div className="review-dialog-actions">
          <button type="button" onClick={onCancel}>取消</button>
          <button type="button" className="review-action-primary" onClick={onConfirm}>应用到本组</button>
        </div>
      </div>
    </div>
  );
}
