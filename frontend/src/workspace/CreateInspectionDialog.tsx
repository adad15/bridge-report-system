import { useEffect, useMemo, useState } from "react";

import { ApiError } from "../api/apiClient";
import { fetchStandardPackages, standardsErrorMessage, type StandardPackageSummary } from "../api/standardsApi";
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
  const [packages, setPackages] = useState<StandardPackageSummary[] | null>(null);
  const [technicalPackageId, setTechnicalPackageId] = useState("");
  const [maintenancePackageId, setMaintenancePackageId] = useState("");

  const technicalPackages = useMemo(
    () => packages?.filter((item) => item.family === "technical_condition" && item.is_enabled && item.sync_status === "正常") ?? [],
    [packages]
  );
  const maintenancePackages = useMemo(
    () => packages?.filter((item) => item.family === "maintenance" && item.is_enabled && item.sync_status === "正常") ?? [],
    [packages]
  );

  useEffect(() => {
    let cancelled = false;
    fetchStandardPackages(backendBaseUrl)
      .then((items) => {
        if (cancelled) return;
        setPackages(items);
        const technical = items.filter((item) => item.family === "technical_condition" && item.is_enabled && item.sync_status === "正常");
        const maintenance = items.filter((item) => item.family === "maintenance" && item.is_enabled && item.sync_status === "正常");
        if (technical.length === 1) setTechnicalPackageId(technical[0].id);
        if (maintenance.length === 1) setMaintenancePackageId(maintenance[0].id);
      })
      .catch((caught) => setError(standardsErrorMessage(caught)));
    return () => { cancelled = true; };
  }, []);

  async function submit() {
    if (!Number.isInteger(year) || year < 1900 || year > 2200) {
      setError("检测年度必须是 1900 至 2200 的整数。");
      return;
    }
    if (!technicalPackageId || !maintenancePackageId) {
      setError("请选择技术状况评定标准和桥涵养护规范。");
      return;
    }
    setSubmitting(true);
    setError(null);
    try {
      const created = await createInspectionYear(backendBaseUrl, bridgeId, {
        inspection_year: year,
        technical_condition_package_id: technicalPackageId,
        maintenance_package_id: maintenancePackageId,
      });
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
        <label>技术状况评定标准
          <select value={technicalPackageId} onChange={(event) => setTechnicalPackageId(event.target.value)} disabled={packages === null || submitting}>
            <option value="">请选择技术状况评定标准</option>
            {technicalPackages.map((item) => <option key={item.id} value={item.id}>{item.standard_code} · {item.official_edition} · 规则包 {item.package_version}</option>)}
          </select>
        </label>
        <label>桥涵养护规范
          <select value={maintenancePackageId} onChange={(event) => setMaintenancePackageId(event.target.value)} disabled={packages === null || submitting}>
            <option value="">请选择桥涵养护规范</option>
            {maintenancePackages.map((item) => <option key={item.id} value={item.id}>{item.standard_code} · {item.official_edition} · 规则包 {item.package_version}</option>)}
          </select>
        </label>
        {packages !== null && (technicalPackages.length === 0 || maintenancePackages.length === 0) ? <p className="warning-text">缺少可用的技术评定标准或养护规范，请联系管理员。</p> : null}
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="dialog-actions"><button type="button" onClick={onClose} disabled={submitting}>取消</button><button type="button" className="primary-button" onClick={() => void submit()} disabled={submitting || packages === null}>{submitting ? "正在创建…" : "创建年度"}</button></div>
      </section>
    </div>
  );
}
