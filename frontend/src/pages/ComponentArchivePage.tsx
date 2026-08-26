import { useEffect, useState } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";

import type { ArchiveObservation, ComponentDefectArchive, ComponentSummary } from "../api/componentArchiveApi";
import { fetchComponentArchive, fetchComponents } from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { ComponentDetailPanel } from "../archive/ComponentDetailPanel";
import { ComponentListPanel } from "../archive/ComponentListPanel";
import { RebindDialog } from "../archive/RebindDialog";
import { backendBaseUrl } from "../config";

// 模块 06 构件病害档案主页面（A1 布局）：左侧构件列表 + 右侧档案详情。
// 只读优先：本页不修改任何年度事实；绑定/重绑经线索整理页或观测行入口发起。
export function ComponentArchivePage() {
  const { bridgeId, componentId } = useParams<{ bridgeId: string; componentId?: string }>();
  const navigate = useNavigate();

  const [components, setComponents] = useState<ComponentSummary[] | null>(null);
  const [listError, setListError] = useState<string | null>(null);
  const [archive, setArchive] = useState<ComponentDefectArchive | null>(null);
  const [archiveError, setArchiveError] = useState<string | null>(null);
  const [rebindTarget, setRebindTarget] = useState<ArchiveObservation | null>(null);
  // 绑定成功后触发档案重载。
  const [archiveVersion, setArchiveVersion] = useState(0);

  useEffect(() => {
    if (!bridgeId) return;
    let cancelled = false;
    fetchComponents(backendBaseUrl, bridgeId)
      .then((items) => {
        if (cancelled) return;
        setComponents(items);
        setListError(null);
      })
      .catch((error) => {
        if (cancelled) return;
        setListError(error instanceof ApiError ? error.message : "构件列表加载失败。");
      });
    return () => {
      cancelled = true;
    };
  }, [bridgeId]);

  useEffect(() => {
    if (!componentId) {
      setArchive(null);
      return;
    }
    let cancelled = false;
    setArchive(null);
    fetchComponentArchive(backendBaseUrl, componentId)
      .then((body) => {
        if (cancelled) return;
        setArchive(body);
        setArchiveError(null);
      })
      .catch((error) => {
        if (cancelled) return;
        setArchiveError(error instanceof ApiError ? error.message : "构件档案加载失败。");
      });
    return () => {
      cancelled = true;
    };
  }, [componentId, archiveVersion]);

  if (!bridgeId) {
    return <p>缺少桥梁标识。</p>;
  }

  const unboundCount = components?.reduce((total, component) => total + component.unbound_count, 0) ?? 0;

  return (
    <section className="archive-page">
      <header className="archive-page-head">
        <h1>构件病害档案</h1>
        <nav className="archive-page-links">
          <Link to={`/bridges/${bridgeId}/defect-threads/triage`}>线索整理</Link>
        </nav>
      </header>
      {unboundCount > 0 ? (
        <div className="archive-pending-banner">
          <span>有 {unboundCount} 条病害观测尚未归入跨年线索。</span>
          <Link to={`/bridges/${bridgeId}/defect-threads/triage`}>进入线索整理</Link>
        </div>
      ) : null}
      <div className="archive-layout">
        {listError ? (
          <p className="archive-empty-hint">{listError}</p>
        ) : components === null ? (
          <p>正在加载构件列表…</p>
        ) : components.length === 0 ? (
          <p className="archive-empty-hint">该桥暂无出现过正式病害的构件。</p>
        ) : (
          <ComponentListPanel
            components={components}
            selectedComponentId={componentId ?? null}
            onSelect={(nextId) => navigate(`/bridges/${bridgeId}/components/${nextId}`)}
          />
        )}
        <div className="archive-detail-slot">
          {!componentId ? (
            <p className="archive-empty-hint">从左侧选择一个构件查看病害档案。</p>
          ) : archiveError ? (
            <p className="archive-empty-hint">{archiveError}</p>
          ) : archive === null ? (
            <p>正在加载构件档案…</p>
          ) : (
            <ComponentDetailPanel
              archive={archive}
              bridgeId={bridgeId}
              onRebind={(observation) => setRebindTarget(observation)}
            />
          )}
        </div>
      </div>
      {rebindTarget !== null && archive !== null ? (
        <RebindDialog
          observation={rebindTarget}
          threads={archive.threads}
          onClose={() => setRebindTarget(null)}
          onSuccess={() => {
            setRebindTarget(null);
            setArchiveVersion((version) => version + 1);
          }}
        />
      ) : null}
    </section>
  );
}
