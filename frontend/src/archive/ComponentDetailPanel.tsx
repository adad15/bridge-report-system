import { Button, Tag } from "antd";
import { useEffect, useState } from "react";

import type { ArchiveObservation, ComponentDefectArchive, RevisionGroup } from "../api/componentArchiveApi";
import { fetchComponentRevisions } from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { backendBaseUrl } from "../config";
import { ComponentRatingSummary } from "./ComponentRatingSummary";
import { DefectThreadCard } from "./DefectThreadCard";
import { ObservationTable } from "./ObservationTable";
import { RevisionHistoryPanel } from "./RevisionHistoryPanel";

// 空线索插画：antd Empty 会带出"暂无数据"文案，这里用固定插画避免多一行无关文字。
function EmptyThreadArt() {
  return (
    <svg className="archive-thread-empty-art" viewBox="0 0 64 48" aria-hidden="true">
      <path d="M14 7l3 4M50 7l-3 4M32 4v5" stroke="#c3d6f7" strokeWidth="2" strokeLinecap="round" fill="none" />
      <path d="M6 15h52v9H6z" fill="#f2f7ff" stroke="#a8c1f2" strokeWidth="2" strokeLinejoin="round" />
      <path d="M10 24h44v16a4 4 0 0 1-4 4H14a4 4 0 0 1-4-4V24Z" fill="#eaf1fe" stroke="#a8c1f2" strokeWidth="2" strokeLinejoin="round" />
      <path d="M26 32h12" stroke="#a8c1f2" strokeWidth="2" strokeLinecap="round" />
    </svg>
  );
}

interface ComponentDetailPanelProps {
  archive: ComponentDefectArchive;
  bridgeId: string;
  /** T14：绑定/重绑入口，由页面注入；只读场景可不提供。 */
  onRebind?: (observation: ArchiveObservation) => void;
}

// A1 右侧构件档案详情（模块 06 §7.2）：构件基本信息 -> 年度评分摘要 ->
// 病害线索卡片（病害一级、年度二级）-> 未绑定观测区 -> 历史修订独立入口。
export function ComponentDetailPanel({ archive, bridgeId, onRebind }: ComponentDetailPanelProps) {
  const [revisionsOpen, setRevisionsOpen] = useState(false);
  const [revisions, setRevisions] = useState<RevisionGroup[] | null>(null);
  const [revisionsError, setRevisionsError] = useState<string | null>(null);

  // 切换构件时收起历史修订视图并丢弃旧数据。
  useEffect(() => {
    setRevisionsOpen(false);
    setRevisions(null);
    setRevisionsError(null);
  }, [archive.component.id]);

  async function toggleRevisions(): Promise<void> {
    const next = !revisionsOpen;
    setRevisionsOpen(next);
    if (!next || revisions !== null) return;
    try {
      setRevisions(await fetchComponentRevisions(backendBaseUrl, archive.component.id));
      setRevisionsError(null);
    } catch (error) {
      setRevisionsError(error instanceof ApiError ? error.message : "历史修订暂不可用。");
    }
  }

  const { component } = archive;
  return (
    <section className="archive-detail-panel">
      <header className="archive-detail-head">
        <div className="archive-detail-identity">
          <div className="archive-component-heading">
            <h2>{component.business_component_code}</h2>
            <Tag color="blue">{component.structure_part}</Tag>
            <Tag>{component.component_type}</Tag>
          </div>
          <p>
            构件编号：{component.system_number}
            <button type="button" className="archive-history-inline" onClick={() => void toggleRevisions()}>
              {revisionsOpen ? "返回当前档案" : "历史修订"}
            </button>
          </p>
        </div>
      </header>

      {revisionsOpen ? (
        <>
          {revisionsError ? <p className="archive-empty-hint">{revisionsError}</p> : null}
          {revisions !== null ? <RevisionHistoryPanel revisions={revisions} /> : <p>正在加载历史修订…</p>}
        </>
      ) : (
        <>
          <section className="archive-detail-section">
            <div className="archive-section-title">
              <h3>年度评分</h3>
            </div>
            <ComponentRatingSummary ratings={archive.ratings} />
          </section>

          <section className="archive-detail-section">
            <div className="archive-section-title">
              <h3>跨年病害线索</h3>
            </div>
            {archive.threads.length === 0 ? (
              <div className="archive-thread-empty">
                <EmptyThreadArt />
                <div className="archive-thread-empty-copy">
                  <strong>尚未形成跨年线索</strong>
                  <p>下方 {archive.unbound_observations.length} 条年度观测待确认是否属于同一处病害。</p>
                </div>
                <Button
                  className="archive-thread-empty-action"
                  type="primary"
                  href={`/bridges/${bridgeId}/defect-threads/triage`}
                >
                  整理该构件
                </Button>
              </div>
            ) : (
              archive.threads.map((thread) => (
                <DefectThreadCard
                  key={thread.id}
                  thread={thread}
                  componentType={component.component_type}
                  onRebind={onRebind}
                />
              ))
            )}
          </section>

          <section className="archive-detail-section">
            <div className="archive-section-title">
              <h3>待整理的年度观测</h3>
              <span className="archive-section-count">{archive.unbound_observations.length}</span>
            </div>
            {archive.unbound_observations.length === 0 ? (
              <p className="archive-empty-hint">当前有效观测均已整理到跨年病害线索。</p>
            ) : (
              <ObservationTable observations={archive.unbound_observations} onRebind={onRebind} />
            )}
          </section>
        </>
      )}
    </section>
  );
}
