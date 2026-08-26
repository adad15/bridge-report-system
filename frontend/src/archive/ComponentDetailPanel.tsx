import { useEffect, useState } from "react";
import { Link } from "react-router-dom";

import type { ArchiveObservation, ComponentDefectArchive, RevisionGroup } from "../api/componentArchiveApi";
import { fetchComponentRevisions } from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { backendBaseUrl } from "../config";
import { ComponentRatingSummary } from "./ComponentRatingSummary";
import { DefectThreadCard } from "./DefectThreadCard";
import { ObservationYearRow } from "./ObservationYearRow";
import { RevisionHistoryPanel } from "./RevisionHistoryPanel";

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
        <div>
          <h2>{component.component_type}</h2>
          <p>
            {component.structure_part}｜{component.business_component_code}｜{component.system_number}
          </p>
        </div>
        <button type="button" onClick={() => void toggleRevisions()}>
          {revisionsOpen ? "返回当前档案" : "历史修订"}
        </button>
      </header>

      {revisionsOpen ? (
        <>
          {revisionsError ? <p className="archive-empty-hint">{revisionsError}</p> : null}
          {revisions !== null ? <RevisionHistoryPanel revisions={revisions} /> : <p>正在加载历史修订…</p>}
        </>
      ) : (
        <>
          <h3>构件年度评分</h3>
          <ComponentRatingSummary ratings={archive.ratings} />

          <h3>病害线索</h3>
          {archive.threads.length === 0 ? (
            <p className="archive-empty-hint">该构件尚无病害线索，可在线索整理页创建。</p>
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

          <h3>未绑定观测</h3>
          {archive.unbound_observations.length === 0 ? (
            <p className="archive-empty-hint">当前有效观测均已绑定病害线索。</p>
          ) : (
            <div className="archive-unbound-block">
              <p className="archive-empty-hint">
                以下观测尚未归入病害线索（未绑定不是错误状态），可前往
                <Link to={`/bridges/${bridgeId}/defect-threads/triage`}>线索整理页</Link>
                统一处理。
              </p>
              {archive.unbound_observations.map((observation) => (
                <ObservationYearRow key={observation.id} observation={observation} onRebind={onRebind} />
              ))}
            </div>
          )}
        </>
      )}
    </section>
  );
}
