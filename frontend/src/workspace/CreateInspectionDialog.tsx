import { useState } from "react";

import { ApiError } from "../api/apiClient";
import { createInspectionYear, workspaceErrorMessage } from "../api/workspaceApi";
import { backendBaseUrl } from "../config";

interface Props {
  bridgeId: string;
  onClose: () => void;
  onCreated: (inspectionYearId: string, reused: boolean) => void;
}

export function CreateInspectionDialog({ bridgeId, onClose, onCreated }: Props) {
  const [year, setYear] = useState(new Date().getFullYear());
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function submit() {
    if (!Number.isInteger(year) || year < 1900 || year > 2200) {
      setError("检测年度必须是 1900 至 2200 的整数。");
      return;
    }
    setSubmitting(true);
    setError(null);
    try {
      const created = await createInspectionYear(backendBaseUrl, bridgeId, year);
      onCreated(created.id, false);
    } catch (caught) {
      if (caught instanceof ApiError && caught.code === "inspection_year_already_exists") {
        const details = caught.details as { existing_inspection_year_id?: unknown } | undefined;
        if (typeof details?.existing_inspection_year_id === "string") {
          onCreated(details.existing_inspection_year_id, true);
          return;
        }
      }
      setError(workspaceErrorMessage(caught));
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <section className="workspace-dialog" role="dialog" aria-modal="true" aria-labelledby="create-year-title">
        <h2 id="create-year-title">新建年度检测</h2>
        <label>检测年度<input type="number" min={1900} max={2200} value={year} onChange={(event) => setYear(Number(event.target.value))} /></label>
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="dialog-actions"><button type="button" onClick={onClose} disabled={submitting}>取消</button><button type="button" className="primary-button" onClick={() => void submit()} disabled={submitting}>{submitting ? "正在创建…" : "创建年度"}</button></div>
      </section>
    </div>
  );
}
