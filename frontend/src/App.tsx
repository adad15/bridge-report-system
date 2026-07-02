import { useEffect, useState } from "react";

import { BackendHealth, fetchBackendHealth } from "./api/health";
import "./styles.css";

const backendBaseUrl =
  import.meta.env.VITE_BACKEND_BASE_URL ?? "http://127.0.0.1:18080";

export function App() {
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
    <main className="app-shell">
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
    </main>
  );
}
