import { useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import { BridgeSummary, fetchBridges } from "../api/navigationApi";
import { backendBaseUrl } from "../config";

export function BridgesPage() {
  const [bridges, setBridges] = useState<BridgeSummary[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const navigate = useNavigate();
  const [query, setQuery] = useState("");

  useEffect(() => {
    let cancelled = false;

    fetchBridges(backendBaseUrl)
      .then((result) => {
        if (cancelled) return;
        setBridges(result);
        setError(null);
      })
      .catch((caught: unknown) => {
        if (cancelled) return;
        setBridges(null);
        setError(caught instanceof ApiError ? caught.message : "加载桥梁列表失败");
      });

    return () => {
      cancelled = true;
    };
  }, []);

  const normalizedQuery = query.trim().toLocaleLowerCase();
  const visibleBridges = bridges?.filter((bridge) =>
    [bridge.bridge_name, bridge.system_number, bridge.route_name ?? ""]
      .some((value) => value.toLocaleLowerCase().includes(normalizedQuery))
  ) ?? null;

  return (
    <section className="status-panel bridges-page">
      <div className="page-title-row"><div><p className="section-kicker">桥梁档案</p><h1>选择一座桥梁</h1><p>进入桥梁后查看最新结论、年度检测和跨年病害档案。</p></div>
        <label className="bridge-search">搜索桥名、编号或路线<input type="search" value={query} onChange={(event) => setQuery(event.target.value)} placeholder="例如：绕阳河、QL-000001、G305" /></label>
      </div>
      {error ? <p className="error-text">{error}</p> : null}
      {!error && bridges === null ? <p>加载中…</p> : null}
      {visibleBridges !== null && bridges?.length === 0 ? <p>暂无桥梁数据。</p> : null}
      {visibleBridges !== null && bridges && bridges.length > 0 && visibleBridges.length === 0 ? <p className="empty-hint">没有匹配的桥梁。</p> : null}
      {visibleBridges !== null && visibleBridges.length > 0 ? (
        <table className="data-table">
          <thead>
            <tr>
              <th>系统编号</th>
              <th>桥名</th>
              <th>路线</th>
              <th>状态</th>
              <th>最新结论</th>
              <th>待办</th>
            </tr>
          </thead>
          <tbody>
            {visibleBridges.map((bridge) => (
              <tr
                key={bridge.id}
                className="data-table-row-clickable"
                onClick={() => navigate(`/bridges/${encodeURIComponent(bridge.id)}`)}
              >
                <td>{bridge.system_number}</td>
                <td>{bridge.bridge_name}</td>
                <td>{bridge.route_name ?? "-"}</td>
                <td>{bridge.status}</td>
                <td>{bridge.latest_inspection_year ? `${bridge.latest_inspection_year} · ${bridge.latest_overall_grade ?? "—"}` : "—"}</td>
                <td>{bridge.pending_count > 0 ? <span className="pending-badge">{bridge.pending_count}</span> : "—"}</td>
              </tr>
            ))}
          </tbody>
        </table>
      ) : null}
    </section>
  );
}
