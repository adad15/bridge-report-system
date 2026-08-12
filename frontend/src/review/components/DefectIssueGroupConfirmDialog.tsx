import type { DefectIssueGroup } from "../defectIssueGroups";

interface DefectIssueGroupConfirmDialogProps {
  group: DefectIssueGroup | null;
  onCancel: () => void;
  onConfirm: () => void;
}

export function DefectIssueGroupConfirmDialog({
  group,
  onCancel,
  onConfirm,
}: DefectIssueGroupConfirmDialogProps) {
  if (!group) return null;

  const confirmableCount = group.rangeSplitConfirmableRows.length;
  const excludedCount = group.rows.length - confirmableCount;

  return (
    <div className="review-dialog-backdrop" role="presentation">
      <div
        className="review-dialog defect-batch-dialog"
        role="dialog"
        aria-modal="true"
        aria-labelledby="defect-issue-group-confirm-title"
      >
        <h3 id="defect-issue-group-confirm-title">确认本组病害</h3>
        <p>
          “{group.title}”中有 {confirmableCount} 条病害可以确认。
        </p>
        <p>无照片记录也会被确认；现有病害选择和照片关联保持不变。</p>
        {excludedCount > 0 ? (
          <p className="warning-text">
            另有 {excludedCount} 条存在其他待处理问题，本次不会确认。
          </p>
        ) : null}
        <p>本次只完成构件范围拆分核对，修改仍需保存草稿后才会提交。</p>
        <div className="review-dialog-actions">
          <button type="button" onClick={onCancel}>取消</button>
          <button
            type="button"
            className="review-action-primary"
            disabled={confirmableCount === 0}
            onClick={onConfirm}
          >
            确认 {confirmableCount} 条
          </button>
        </div>
      </div>
    </div>
  );
}
