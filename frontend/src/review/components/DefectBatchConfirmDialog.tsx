interface DefectBatchConfirmDialogProps {
  open: boolean;
  defectCount: number;
  photoCount: number;
  removedCount: number;
  /** 待确认记录的规范病害分布，确认前先让用户看清这一批到底是什么。 */
  defectNameDistribution?: Array<{ name: string; count: number }>;
  onCancel: () => void;
  onConfirm: () => void;
}

export function DefectBatchConfirmDialog({
  open,
  defectCount,
  photoCount,
  removedCount,
  defectNameDistribution = [],
  onCancel,
  onConfirm,
}: DefectBatchConfirmDialogProps) {
  if (!open) return null;
  return (
    <div className="review-dialog-backdrop" role="presentation">
      <div className="review-dialog defect-batch-dialog" role="dialog" aria-modal="true" aria-labelledby="defect-batch-title">
        <h3 id="defect-batch-title">确认安全病害</h3>
        <p>将确认 {defectCount} 条病害及 {photoCount} 张唯一高置信照片。</p>
        {defectNameDistribution.length > 0 ? (
          <ul className="defect-batch-distribution" aria-label="规范病害分布">
            {defectNameDistribution.map((item) => (
              <li key={item.name}><span>{item.name}</span><strong>{item.count}</strong></li>
            ))}
          </ul>
        ) : null}
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
