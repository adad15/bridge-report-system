import { createContext, useContext, useEffect, useMemo, useState } from "react";
import { Link, NavLink, Outlet, useParams } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import { fetchBridgeOverview, type BridgeOverview } from "../api/workspaceApi";
import { backendBaseUrl } from "../config";
import {
  bridgeOverviewPath,
  componentArchivePath,
  componentInventoryPath,
  inspectionsPath,
} from "./workspaceState";

interface BridgeWorkspaceContextValue {
  overview: BridgeOverview;
  reloadOverview: () => void;
}

const BridgeWorkspaceContext = createContext<BridgeWorkspaceContextValue | null>(null);

export function useBridgeWorkspace(): BridgeWorkspaceContextValue {
  const value = useContext(BridgeWorkspaceContext);
  if (value === null) throw new Error("useBridgeWorkspace must be used inside BridgeWorkspaceShell");
  return value;
}

export function BridgeWorkspaceShell() {
  const { bridgeId } = useParams<{ bridgeId: string }>();
  const [overview, setOverview] = useState<BridgeOverview | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [version, setVersion] = useState(0);

  useEffect(() => {
    if (!bridgeId) return;
    let cancelled = false;
    setOverview(null);
    setError(null);
    fetchBridgeOverview(backendBaseUrl, bridgeId)
      .then((body) => {
        if (!cancelled) setOverview(body);
      })
      .catch((caught: unknown) => {
        if (!cancelled) setError(caught instanceof ApiError ? caught.message : "桥梁工作区加载失败。");
      });
    return () => {
      cancelled = true;
    };
  }, [bridgeId, version]);

  const context = useMemo(
    () => (overview ? { overview, reloadOverview: () => setVersion((current) => current + 1) } : null),
    [overview]
  );

  if (!bridgeId) return <section className="status-panel"><p className="error-text">缺少桥梁标识。</p></section>;
  if (error) {
    return (
      <section className="status-panel">
        <h1>无法打开桥梁档案</h1>
        <p className="error-text">{error}</p>
        <button type="button" onClick={() => setVersion((current) => current + 1)}>重新加载</button>
        <Link to="/bridges">返回桥梁档案</Link>
      </section>
    );
  }
  if (!overview || !context) return <section className="status-panel"><p>正在加载桥梁档案…</p></section>;

  const bridge = overview.bridge;
  return (
    <div className="bridge-workspace-shell">
      <header className="bridge-workspace-header">
        <div className="bridge-breadcrumb"><Link to="/bridges">桥梁档案</Link><span>/</span><span>{bridge.bridge_name}</span></div>
        <div className="bridge-title-row">
          <div>
            <h1>{bridge.bridge_name}</h1>
            <p>{bridge.system_number} · {bridge.route_name ?? "路线未填写"}</p>
          </div>
          <span className="status-badge">{bridge.status}</span>
        </div>
        <nav className="bridge-tabs" aria-label="桥梁工作区">
          <NavLink end to={bridgeOverviewPath(bridge.id)}>桥梁概览</NavLink>
          <NavLink to={componentInventoryPath(bridge.id)}>构件台账</NavLink>
          <NavLink to={inspectionsPath(bridge.id)}>年度检测</NavLink>
          <NavLink to={componentArchivePath(bridge.id)}>构件病害档案</NavLink>
        </nav>
      </header>
      <BridgeWorkspaceContext.Provider value={context}>
        <Outlet />
      </BridgeWorkspaceContext.Provider>
    </div>
  );
}
