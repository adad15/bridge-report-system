interface ReviewActionBarProps {
  onSaveDraft?: () => void;
  onBatchConfirmNormal?: () => void;
  onPreflight?: () => void;
  onConfirmImport?: () => void;
  onCancelImport?: () => void;
}

// 五个操作按钮（模块 05 §7.5）。本任务（Task 13）只负责渲染；按钮行为在 Task 14 接线。
// 一个按钮是否可用完全取决于调用方是否传了对应的 handler：不传 -> 禁用，这样
// Task 13 天然满足"先渲染并禁用未接线按钮"的要求，Task 14 只需要传入 handler 即可解锁。
export function ReviewActionBar({
  onSaveDraft,
  onBatchConfirmNormal,
  onPreflight,
  onConfirmImport,
  onCancelImport,
}: ReviewActionBarProps) {
  return (
    <div className="status-panel review-action-bar">
      <button type="button" disabled={!onSaveDraft} onClick={onSaveDraft}>
        保存草稿
      </button>
      <button type="button" disabled={!onBatchConfirmNormal} onClick={onBatchConfirmNormal}>
        批量确认普通候选
      </button>
      <button type="button" disabled={!onPreflight} onClick={onPreflight}>
        入库前检查
      </button>
      <button type="button" disabled={!onConfirmImport} onClick={onConfirmImport}>
        确认年度事实入库
      </button>
      <button type="button" disabled={!onCancelImport} onClick={onCancelImport}>
        取消导入
      </button>
    </div>
  );
}
