import type {
  BindingTarget,
  ComponentRangeSplitPreview,
} from "../../api/importBindingApi";

export function ComponentRangeSplitDialog({
  preview,
  targets,
  busy,
  error,
  onClose,
  onApply,
}: {
  preview: ComponentRangeSplitPreview;
  targets: BindingTarget[];
  busy: boolean;
  error: string | null;
  onClose: () => void;
  onApply: (targets: BindingTarget[], impactToken: string) => void;
}) {
  const { totals } = preview;
  return (
    <div className="dialog-backdrop" role="presentation">
      <section className="workspace-dialog range-split-dialog" role="dialog" aria-modal="true" aria-labelledby="range-split-title">
        <h2 id="range-split-title">拆分构件范围</h2>
        <p className="dialog-hint">
          拆分后每个实际构件会分别参与评分，病害数量增加可能使总扣分增加。
          每条新病害会复制原文字、标度和照片候选，并标记为待人工核对。
        </p>
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="range-split-table-wrap">
          <table className="data-table">
            <thead>
              <tr>
                <th>原构件范围</th><th>构件数</th><th>病害</th><th>照片</th><th>绑定结果</th>
              </tr>
            </thead>
            <tbody>
              {preview.items.map((item) => (
                <tr key={`${item.part_name}\n${item.component_number}`}>
                  <td>{item.component_number}</td>
                  <td>{item.expanded_component_count}</td>
                  <td>{item.source_defect_count} → {item.result_defect_count}</td>
                  <td>{item.result_photo_count}</td>
                  <td>
                    自动绑定 {item.bound_count} · 歧义 {item.ambiguous_count} · 未匹配 {item.unmatched_count}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p className="range-split-totals">
          共拆分 {totals.selected_range_count} 个范围，生成 {totals.result_defect_count} 条病害、
          {totals.result_photo_count} 条照片候选。
        </p>
        <div className="dialog-actions">
          <button type="button" disabled={busy} onClick={onClose}>取消</button>
          <button
            type="button"
            className="primary-button"
            disabled={busy}
            onClick={() => onApply(targets, preview.impact_token)}
          >
            {busy ? "正在应用…" : "确认拆分"}
          </button>
        </div>
      </section>
    </div>
  );
}
