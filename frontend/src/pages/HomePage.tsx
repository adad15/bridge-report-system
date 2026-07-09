import { useEffect, useState } from "react";

import { BackendHealth, fetchBackendHealth } from "../api/health";
import { backendBaseUrl } from "../config";

export function HomePage() {
  const [health, setHealth] = useState<BackendHealth | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    fetchBackendHealth(backendBaseUrl)
      .then((result) => {
        setHealth(result);
        setError(null);
      })
      .catch((caught: unknown) => {
        setHealth(null);
        setError(caught instanceof Error ? caught.message : "Unknown health check error");
      });
  }, []);

  return (
    <section className="status-panel">
      <h1>桥梁报告系统</h1>
      <div className="status-row">
        <span>C++ 主服务</span>
        <strong>{health?.status ?? "checking"}</strong>
      </div>
      <div className="status-grid">
        <span>服务</span>
        <span>{health?.service ?? "-"}</span>
        <span>版本</span>
        <span>{health?.version ?? "-"}</span>
        <span>Python 工具服务</span>
        <span>{health?.python_tools_base_url ?? "-"}</span>
        <span>归档目录</span>
        <span>{health?.archive_root ?? "-"}</span>
      </div>
      {error ? <p className="error-text">{error}</p> : null}
    </section>
  );
}
