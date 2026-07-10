interface ReviewActionBarProps {
  onSaveDraft?: () => void;
  onBatchConfirmNormal?: () => void;
  onPreflight?: () => void;
  onConfirmImport?: () => void;
  onCancelImport?: () => void;
}

// 五个操作按钮（模块 05 §7.5）。本组件保持纯展示：一个按钮是否可用完全取决于调用方
// （ReviewWorkspacePage）是否传了对应的 handler——不传 -> 禁用。保存草稿/批量确认/
// 入库前检查/确认入库/取消导入的业务逻辑、加载中状态和只读状态全部在页面层维护
// （只读或请求进行中时，页面层直接不传 handler，本组件不需要认识 busy/readOnly 这类概念）。
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
