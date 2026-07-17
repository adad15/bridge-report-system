import { useCallback, useEffect, useState } from "react";

import {
  fetchStandardPackages,
  setStandardPackageEnabled,
  standardsErrorMessage,
  type StandardPackageSummary,
} from "../api/standardsApi";
import { backendBaseUrl } from "../config";

interface Props {
  onClose: () => void;
}

const familyName = (family: StandardPackageSummary["family"]) =>
  family === "technical_condition" ? "公路桥梁技术状况评定标准" : "公路桥涵养护规范";

export function StandardsAdminPanel({ onClose }: Props) {
  const [packages, setPackages] = useState<StandardPackageSummary[] | null>(null);
  const [busyId, setBusyId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(() => {
    setError(null);
    fetchStandardPackages(backendBaseUrl)
      .then(setPackages)
      .catch((caught) => setError(standardsErrorMessage(caught)));
  }, []);

  useEffect(load, [load]);

  async function toggle(item: StandardPackageSummary) {
    setBusyId(item.id);
    setError(null);
    try {
      const updated = await setStandardPackageEnabled(backendBaseUrl, item.id, !item.is_enabled);
      setPackages((current) => current?.map((value) => value.id === updated.id ? updated : value) ?? null);
    } catch (caught) {
      setError(standardsErrorMessage(caught));
    } finally {
      setBusyId(null);
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <section className="workspace-dialog standards-admin-panel" role="dialog" aria-modal="true" aria-labelledby="standards-title">
        <h2 id="standards-title">规范管理</h2>
        <p>停用只影响新建年度；历史年度仍保留原规范版本。</p>
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        {packages === null && !error ? <p>正在加载规范目录…</p> : null}
        {packages?.map((item) => (
          <article className="standard-package-row" key={item.id}>
            <div>
              <strong>{item.standard_code} · {item.official_edition}</strong>
              <p>{familyName(item.family)} · 规则包 {item.package_version}</p>
              <span className={item.sync_status === "正常" ? "status-badge" : "warning-badge"}>
                {item.sync_status === "正常" ? (item.is_enabled ? "已启用" : "已停用") : "故障"}
              </span>
            </div>
            <button
              type="button"
              disabled={busyId !== null || item.sync_status === "故障"}
              onClick={() => void toggle(item)}
            >
              {busyId === item.id ? "正在保存…" : item.is_enabled ? "停用" : "启用"}
            </button>
          </article>
        ))}
        <div className="dialog-actions"><button type="button" onClick={onClose} disabled={busyId !== null}>关闭</button></div>
      </section>
    </div>
  );
}
