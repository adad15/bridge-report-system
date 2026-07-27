interface DefectBatchConfirmDialogProps {
  open: boolean;
  defectCount: number;
  photoCount: number;
  removedCount: number;
  onCancel: () => void;
  onConfirm: () => void;
}

export function DefectBatchConfirmDialog({
  open,
  defectCount,
  photoCount,
  removedCount,
  onCancel,
  onConfirm,
}: DefectBatchConfirmDialogProps) {
  if (!open) return null;
  return (
    <div className="review-dialog-backdrop" role="presentation">
      <div className="review-dialog defect-batch-dialog" role="dialog" aria-modal="true" aria-labelledby="defect-batch-title">
        <h3 id="defect-batch-title">确认安全病害</h3>
        <p>将确认 {defectCount} 条病害及 {photoCount} 张唯一高置信照片。</p>
        {removedCount > 0 ? <p className="warning-text">有 {removedCount} 条因条件变化已自动移除，不会被修改。</p> : null}
        <p>此操作只更新当前校对草稿，不会自动保存或正式入库。</p>
        <div className="review-dialog-actions">
          <button type="button" onClick={onCancel}>取消</button>
          <button type="button" className="review-action-primary" disabled={defectCount === 0} onClick={onConfirm}>确认所选</button>
        </div>
      </div>
    </div>
  );
}
