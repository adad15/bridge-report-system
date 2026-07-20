import { useEffect, useState } from "react";

import { ApiError } from "../api/apiClient";
import {
  deleteImportRecord,
  fetchImportRecordDeletionImpact,
  type DeleteImportRecordResult,
  type ImportRecordDeletionImpact,
  workspaceErrorMessage,
} from "../api/workspaceApi";
import { backendBaseUrl } from "../config";

interface Props {
  importRecordId: string;
  onClose: () => void;
  onDeleted: (result: DeleteImportRecordResult) => void;
}

function blockMessage(impact: ImportRecordDeletionImpact): string | null {
  if (impact.block_code === "import_record_edit_locked") return "当前有人正在编辑，必须等编辑者退出或编辑锁过期后才能删除。";
  if (impact.block_code === "import_record_has_formal_facts") return "该记录已经形成正式病害、照片或评分事实，不能单独删除。请使用年度删除能力。";
  if (impact.block_code === "import_record_not_deletable") return "该记录已进入正式只读状态，不能单独删除。请使用年度删除能力。";
  return impact.can_delete ? null : "当前导入记录不能删除。";
}

export function DeleteImportRecordDialog({ importRecordId, onClose, onDeleted }: Props) {
  const [impact, setImpact] = useState<ImportRecordDeletionImpact | null>(null);
  const [reason, setReason] = useState("");
  const [confirmation, setConfirmation] = useState("");
  const [loading, setLoading] = useState(true);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const loadImpact = () => {
    setLoading(true);
    setError(null);
    fetchImportRecordDeletionImpact(backendBaseUrl, importRecordId)
      .then(setImpact)
      .catch((caught) => setError(workspaceErrorMessage(caught)))
      .finally(() => setLoading(false));
  };

  useEffect(loadImpact, [importRecordId]);

  async function submit() {
    if (!impact) return;
    setSubmitting(true);
    setError(null);
    try {
      const result = await deleteImportRecord(backendBaseUrl, importRecordId, {
        impact_token: impact.impact_token,
        confirmation_text: confirmation,
        reason: reason.trim(),
      });
      onDeleted(result);
    } catch (caught) {
      setError(workspaceErrorMessage(caught));
      if (caught instanceof ApiError && (
        caught.code === "deletion_impact_changed" || caught.code === "import_record_edit_locked"
        || caught.code === "import_record_has_formal_facts" || caught.code === "import_record_not_deletable"
      )) {
        setConfirmation("");
        try {
          setImpact(await fetchImportRecordDeletionImpact(backendBaseUrl, importRecordId));
        } catch {
          // 保留原冲突提示；用户也可以关闭弹窗后重新打开。
        }
      }
    } finally {
      setSubmitting(false);
    }
  }

  const blocked = impact ? blockMessage(impact) : null;
  const canDelete = impact?.can_delete === true && reason.trim().length > 0
    && confirmation === impact.confirmation_text && !submitting;

  return (
    <div className="dialog-backdrop" role="presentation">
      <section className="workspace-dialog delete-import-dialog" role="dialog" aria-modal="true" aria-labelledby="delete-import-title">
        <h2 id="delete-import-title">永久删除导入记录</h2>
        {loading ? <p>正在核对删除影响…</p> : null}
        {impact ? <>
          <div className="danger-callout">
            <strong>此操作不可撤销</strong>
            <p>只删除“{impact.import_record.import_name}”（{impact.import_record.system_number}），不会删除 {impact.inspection_year.year} 年度检测或其他导入记录。</p>
          </div>
          <p className="muted-text">{impact.bridge.bridge_name} · {impact.inspection_year.year} 年 V{impact.inspection_year.version_number} · {impact.import_record.status}</p>
          <dl className="deletion-impact-grid">
            <div><dt>候选病害</dt><dd>{impact.counts.defects}</dd></div>
            <div><dt>照片候选</dt><dd>{impact.counts.photos}</dd></div>
            <div><dt>归档文件</dt><dd>{impact.counts.archived_files_to_delete}</dd></div>
            <div><dt>临时 Word</dt><dd>{impact.counts.temporary_word_files_to_delete}</dd></div>
            <div><dt>解析工作目录</dt><dd>{impact.counts.parse_work_directories_to_delete}</dd></div>
          </dl>
          {impact.counts.shared_files_retained > 0 ? <p className="muted-text">另有 {impact.counts.shared_files_retained} 个共享文件仍被其他资料引用，将保留。</p> : null}
          {blocked ? <div className="lock-warning" role="alert">
            <strong>当前不能删除</strong><p>{blocked}</p>
            {impact.active_edit_locks.map((lock) => <p key={lock.import_record_id}>{lock.owner_display_name}（{lock.owner_username}）正在编辑。</p>)}
          </div> : null}
          <label>删除原因<textarea value={reason} maxLength={1000} onChange={(event) => setReason(event.target.value)} placeholder="例如：重复上传、误选报告" /></label>
          <label>请输入“{impact.confirmation_text}”确认<input value={confirmation} onChange={(event) => setConfirmation(event.target.value)} autoComplete="off" /></label>
        </> : null}
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="dialog-actions">
          <button type="button" onClick={onClose} disabled={submitting}>取消</button>
          {error && !impact ? <button type="button" onClick={loadImpact}>重新加载</button> : null}
          <button type="button" className="danger-button" onClick={() => void submit()} disabled={!canDelete}>
            {submitting ? "正在永久删除…" : "永久删除此导入记录"}
          </button>
        </div>
      </section>
    </div>
  );
}
