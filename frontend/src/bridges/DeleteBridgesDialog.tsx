import { useEffect, useMemo, useState } from "react";

import {
  bridgeAdministrationError,
  deleteBridges,
  fetchBridgeDeletionImpact,
  type BridgeDeletionPreview,
  type DeleteBridgesResult,
} from "../api/bridgeAdministrationApi";
import { backendBaseUrl } from "../config";
import { ApiError } from "../api/apiClient";

interface Props {
  bridgeIds: string[];
  onClose: () => void;
  onSelectionChanged: () => void;
  onCompleted: () => void;
}

export function DeleteBridgesDialog({ bridgeIds, onClose, onSelectionChanged, onCompleted }: Props) {
  const [preview, setPreview] = useState<BridgeDeletionPreview | null>(null);
  const [reason, setReason] = useState("");
  const [confirmation, setConfirmation] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<DeleteBridgesResult | null>(null);

  useEffect(() => {
    fetchBridgeDeletionImpact(backendBaseUrl, bridgeIds)
      .then(setPreview)
      .catch(() => setError("删除影响加载失败。"));
  }, [bridgeIds]);

  const canDelete = useMemo(
    () => Boolean(preview && reason.trim() && confirmation === preview.confirmation_text && !busy),
    [preview, reason, confirmation, busy]
  );

  async function submit() {
    if (!preview) return;
    setBusy(true);
    setError(null);
    try {
      setResult(await deleteBridges(backendBaseUrl, {
        confirmation_text: confirmation,
        reason: reason.trim(),
        items: preview.bridges.map((item) => ({
          bridge_id: item.bridge.id,
          impact_token: item.impact_token,
        })),
      }));
      onCompleted();
    } catch (caught) {
      setError(bridgeAdministrationError(caught));
      if (caught instanceof ApiError && caught.code === "bridge_selection_changed") {
        onSelectionChanged();
      }
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <section className="workspace-dialog delete-bridges-dialog" role="dialog" aria-modal="true" aria-labelledby="delete-bridges-title">
        <h2 id="delete-bridges-title">永久删除桥梁档案</h2>
        {!preview && !error ? <p>正在核对删除影响…</p> : null}
        {preview && !result ? (
          <>
            <div className="danger-callout">
              <strong>此操作不可撤销</strong>
              <p>将逐座永久删除所选桥梁的全部年度、导入、病害、评分、独占归档文件和临时来源文件。</p>
            </div>
            {preview.bridges.map((item) => (
              <article className="bridge-delete-impact" key={item.bridge.id}>
                <h3>{item.bridge.system_number}　{item.bridge.bridge_name}</h3>
                <p>
                  年度 {item.counts.inspection_years} · 版本 {item.counts.inspection_versions} ·
                  导入 {item.counts.import_records} · 构件 {item.counts.bridge_components} ·
                  病害 {item.counts.defect_observations} · 正式文件 {item.counts.archived_files_to_delete} ·
                  临时文件 {item.counts.temporary_source_files_to_delete}
                </p>
                {item.active_edit_locks.map((lock) => (
                  <p className="lock-warning" key={lock.import_record_id}>{lock.owner_display_name} 正在编辑，本次不能删除</p>
                ))}
              </article>
            ))}
            <label>删除原因<textarea maxLength={4000} value={reason} onChange={(event) => setReason(event.target.value)} /></label>
            <label>请输入“{preview.confirmation_text}”确认<input value={confirmation} onChange={(event) => setConfirmation(event.target.value)} /></label>
          </>
        ) : null}
        {result ? (
          <div>
            <h3>已处理</h3>
            {result.results.map((item) => (
              <p key={item.bridge_id}>
                {item.status === "deleted" ? "✓" : "✗"} {item.system_number} {item.bridge_name}：
                {item.status === "deleted" && item.file_cleanup_status === "pending"
                  ? `业务档案已完整删除；${item.pending_file_count} 个孤立归档文件等待后台清理`
                  : item.status === "deleted" ? "已删除" : item.message}
              </p>
            ))}
          </div>
        ) : null}
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="dialog-actions">
          <button type="button" disabled={busy} onClick={onClose}>{result ? "关闭" : "取消"}</button>
          {!result ? <button className="danger-button" type="button" disabled={!canDelete} onClick={() => void submit()}>{busy ? "正在删除…" : "永久删除"}</button> : null}
        </div>
      </section>
    </div>
  );
}
