import { useEffect, useState } from "react";

import { ApiError } from "../api/apiClient";
import {
  deleteInspectionYear,
  fetchInspectionYearDeletionImpact,
  type DeleteInspectionYearResult,
  type InspectionYearDeletionImpact,
  workspaceErrorMessage,
} from "../api/workspaceApi";
import { backendBaseUrl } from "../config";

interface Props {
  inspectionYearId: string;
  onClose: () => void;
  onDeleted: (result: DeleteInspectionYearResult) => void;
}

export function DeleteInspectionYearDialog({ inspectionYearId, onClose, onDeleted }: Props) {
  const [impact, setImpact] = useState<InspectionYearDeletionImpact | null>(null);
  const [reason, setReason] = useState("");
  const [confirmation, setConfirmation] = useState("");
  const [loading, setLoading] = useState(true);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const loadImpact = () => {
    setLoading(true);
    setError(null);
    fetchInspectionYearDeletionImpact(backendBaseUrl, inspectionYearId)
      .then(setImpact)
      .catch((caught) => setError(workspaceErrorMessage(caught)))
      .finally(() => setLoading(false));
  };

  useEffect(loadImpact, [inspectionYearId]);

  async function submit() {
    if (!impact) return;
    setSubmitting(true);
    setError(null);
    try {
      const result = await deleteInspectionYear(backendBaseUrl, inspectionYearId, {
        impact_token: impact.impact_token,
        confirmation_text: confirmation,
        reason: reason.trim(),
      });
      onDeleted(result);
    } catch (caught) {
      setError(workspaceErrorMessage(caught));
      if (caught instanceof ApiError
        && (caught.code === "deletion_impact_changed" || caught.code === "inspection_year_edit_locked")) {
        setConfirmation("");
        try {
          setImpact(await fetchInspectionYearDeletionImpact(backendBaseUrl, inspectionYearId));
        } catch {
          // 保留原始冲突提示；关闭并重新打开弹窗仍可再次加载。
        }
      }
    } finally {
      setSubmitting(false);
    }
  }

  const locked = (impact?.active_edit_locks.length ?? 0) > 0;
  const canDelete = impact !== null && !locked && reason.trim().length > 0
    && confirmation === impact.confirmation_text && !submitting;

  return (
    <div className="dialog-backdrop" role="presentation">
      <section className="workspace-dialog delete-year-dialog" role="dialog" aria-modal="true" aria-labelledby="delete-year-title">
        <h2 id="delete-year-title">永久删除年度检测</h2>
        {loading ? <p>正在核对删除影响…</p> : null}
        {impact ? <>
          <div className="danger-callout">
            <strong>此操作不可撤销</strong>
            <p>将永久删除“{impact.bridge.bridge_name}”{impact.inspection_year} 年的全部版本（{impact.version_numbers.map((item) => `V${item}`).join("、")}），不是只删除当前版本。</p>
          </div>
          <dl className="deletion-impact-grid">
            <div><dt>年度版本</dt><dd>{impact.counts.inspection_versions}</dd></div>
            <div><dt>导入记录</dt><dd>{impact.counts.import_records}</dd></div>
            <div><dt>病害观测</dt><dd>{impact.counts.defect_observations}</dd></div>
            <div><dt>病害照片</dt><dd>{impact.counts.defect_photos}</dd></div>
            <div><dt>评分记录</dt><dd>{impact.counts.condition_ratings}</dd></div>
            <div><dt>正式归档文件</dt><dd>{impact.counts.archived_files_to_delete}</dd></div>
            <div><dt>临时来源文件</dt><dd>{impact.counts.temporary_source_files_to_delete}</dd></div>
          </dl>
          {impact.counts.shared_files_retained > 0 ? <p className="muted-text">另有 {impact.counts.shared_files_retained} 个共享文件仍被其他资料引用，将保留。</p> : null}
          {locked ? <div className="lock-warning" role="alert">
            <strong>当前不能删除</strong>
            {impact.active_edit_locks.map((lock) => <p key={lock.import_record_id}>{lock.owner_display_name}（{lock.owner_username}）正在编辑该年度的导入记录。</p>)}
          </div> : null}
          <label>删除原因<textarea value={reason} maxLength={1000} onChange={(event) => setReason(event.target.value)} placeholder="例如：误建年度、测试数据需要清除" /></label>
          <label>请输入“{impact.confirmation_text}”确认<input value={confirmation} onChange={(event) => setConfirmation(event.target.value)} autoComplete="off" /></label>
        </> : null}
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="dialog-actions">
          <button type="button" onClick={onClose} disabled={submitting}>取消</button>
          {error && !impact ? <button type="button" onClick={loadImpact}>重新加载</button> : null}
          <button type="button" className="danger-button" onClick={() => void submit()} disabled={!canDelete}>
            {submitting ? "正在永久删除…" : "永久删除"}
          </button>
        </div>
      </section>
    </div>
  );
}
