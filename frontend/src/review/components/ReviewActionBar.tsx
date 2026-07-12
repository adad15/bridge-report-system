interface ReviewActionBarProps {
  onSaveDraft?: () => void;
  onBatchConfirmNormal?: () => void;
  onPreflight?: () => void;
  onConfirmImport?: () => void;
  onCancelImport?: () => void;
  dirty?: boolean;
  readOnlyNotice?: string;
  onBackToBridge?: () => void;
}

// 底部操作栏（模块 05 §7.5，布局见 2026-07-12 布局设计 §9）。本组件保持纯展示：一个按钮
// 是否可用完全取决于调用方（ReviewWorkspacePage）是否传了对应的 handler——不传 -> 禁用。
// 业务逻辑、加载中状态和只读状态全部在页面层维护（只读时页面层传 readOnlyNotice，本栏
// 整条换成只读横幅 + 返回按钮，不再渲染操作按钮）。
export function ReviewActionBar({
  onSaveDraft,
  onBatchConfirmNormal,
  onPreflight,
  onConfirmImport,
  onCancelImport,
  dirty = false,
  readOnlyNotice,
  onBackToBridge,
}: ReviewActionBarProps) {
  if (readOnlyNotice) {
    return (
      <div className="review-action-bar review-action-bar-readonly">
        <p>{readOnlyNotice}</p>
        <button type="button" onClick={onBackToBridge}>
          返回桥梁详情
        </button>
      </div>
    );
  }

  return (
    <div className="review-action-bar">
      <button type="button" className="review-action-cancel" disabled={!onCancelImport} onClick={onCancelImport}>
        取消导入
      </button>
      <span className="review-action-dirty">{dirty ? "● 有未保存的修改，请先保存草稿再进行入库前检查" : null}</span>
      <button type="button" disabled={!onSaveDraft} onClick={onSaveDraft}>
        保存草稿
      </button>
      <button type="button" disabled={!onBatchConfirmNormal} onClick={onBatchConfirmNormal}>
        批量确认普通评分
      </button>
      <button type="button" disabled={!onPreflight} onClick={onPreflight}>
        入库前检查
      </button>
      <button type="button" className="review-action-primary" disabled={!onConfirmImport} onClick={onConfirmImport}>
        确认年度事实入库
      </button>
    </div>
  );
}
