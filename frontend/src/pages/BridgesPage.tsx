import { useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import { BridgeSummary, fetchBridges } from "../api/navigationApi";
import { backendBaseUrl } from "../config";

export function BridgesPage() {
  const [bridges, setBridges] = useState<BridgeSummary[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const navigate = useNavigate();

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

  return (
    <section className="status-panel">
      <h1>桥梁列表</h1>
      {error ? <p className="error-text">{error}</p> : null}
      {!error && bridges === null ? <p>加载中…</p> : null}
      {bridges !== null && bridges.length === 0 ? <p>暂无桥梁数据。</p> : null}
      {bridges !== null && bridges.length > 0 ? (
        <table className="data-table">
          <thead>
            <tr>
              <th>系统编号</th>
              <th>桥名</th>
              <th>路线</th>
              <th>状态</th>
            </tr>
          </thead>
          <tbody>
            {bridges.map((bridge) => (
              <tr
                key={bridge.id}
                className="data-table-row-clickable"
                onClick={() => navigate(`/bridges/${encodeURIComponent(bridge.id)}`)}
              >
                <td>{bridge.system_number}</td>
                <td>{bridge.bridge_name}</td>
                <td>{bridge.route_name ?? "-"}</td>
                <td>{bridge.status}</td>
              </tr>
            ))}
          </tbody>
        </table>
      ) : null}
    </section>
  );
}
