import { Button, Card, Result } from "antd";
import { createContext, useContext, useEffect, useMemo, useState } from "react";
import { Link, Outlet, useParams } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import { fetchBridgeOverview, type BridgeOverview } from "../api/workspaceApi";
import { backendBaseUrl } from "../config";
import { useHeaderBridge } from "../layouts/HeaderBridgeContext";

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

  // 桥名、状态、编号挂在顶栏上，页面里不再单独占一张卡片。
  const bridgeSummary = overview?.bridge;
  useHeaderBridge(bridgeSummary ? {
    id: bridgeSummary.id,
    name: bridgeSummary.bridge_name,
    status: bridgeSummary.status,
    systemNumber: bridgeSummary.system_number,
    routeName: bridgeSummary.route_name,
  } : null);

  const context = useMemo(
    () => (overview ? { overview, reloadOverview: () => setVersion((current) => current + 1) } : null),
    [overview]
  );

  if (!bridgeId) return <Result status="error" title="缺少桥梁标识。" />;
  if (error) {
    return (
      <Card>
        <Result
          status="error"
          title="无法打开桥梁档案"
          subTitle={error}
          extra={[
            <Button key="reload" type="primary" onClick={() => setVersion((current) => current + 1)}>重新加载</Button>,
            <Link key="back" to="/bridges">返回桥梁档案</Link>,
          ]}
        />
      </Card>
    );
  }
  if (!overview || !context) return <Card loading>正在加载桥梁档案…</Card>;

  return (
    <BridgeWorkspaceContext.Provider value={context}>
      <Outlet />
    </BridgeWorkspaceContext.Provider>
  );
}
