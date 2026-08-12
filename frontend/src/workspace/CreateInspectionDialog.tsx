import { useEffect, useState } from "react";

import { ApiError } from "../api/apiClient";
import {
  createInspectionYear,
  fetchRatingTreeVersions,
  workspaceErrorMessage,
  type RatingTreeVersionSummary,
} from "../api/workspaceApi";
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
  const [trees, setTrees] = useState<RatingTreeVersionSummary[] | null>(null);
  const [ratingTreeVersionId, setRatingTreeVersionId] = useState("");

  useEffect(() => {
    let cancelled = false;
    fetchRatingTreeVersions(backendBaseUrl)
      .then((items) => {
        if (cancelled) return;
        setTrees(items);
        const defaultTree = items.find((item) => item.is_default) ??
          (items.length === 1 ? items[0] : undefined);
        if (defaultTree) setRatingTreeVersionId(defaultTree.id);
      })
      .catch((caught) => setError(workspaceErrorMessage(caught)));
    return () => { cancelled = true; };
  }, []);

  async function submit() {
    if (!Number.isInteger(year) || year < 1900 || year > 2200) {
      setError("检测年度必须是 1900 至 2200 的整数。");
      return;
    }
    if (!ratingTreeVersionId) {
      setError("请选择桥梁评定树。");
      return;
    }
    setSubmitting(true);
    setError(null);
    try {
      const created = await createInspectionYear(backendBaseUrl, bridgeId, {
        inspection_year: year,
        rating_tree_version_id: ratingTreeVersionId,
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
        <label>桥梁评定树
          <select value={ratingTreeVersionId} onChange={(event) => setRatingTreeVersionId(event.target.value)} disabled={trees === null || submitting}>
            <option value="">请选择桥梁评定树</option>
            {trees?.map((item) => (
              <option key={item.id} value={item.id}>
                {item.tree_name} · 树 {item.package_version} · H21 {item.h21_package_version} · JTG 5120 {item.maintenance_package_version}
              </option>
            ))}
          </select>
        </label>
        {trees !== null && trees.length === 0 ? <p className="warning-text">缺少已发布的桥梁评定树，请检查后端规则同步状态。</p> : null}
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="dialog-actions"><button type="button" onClick={onClose} disabled={submitting}>取消</button><button type="button" className="primary-button" onClick={() => void submit()} disabled={submitting || trees === null}>{submitting ? "正在创建…" : "创建年度"}</button></div>
      </section>
    </div>
  );
}
