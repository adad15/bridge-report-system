import {
  ApartmentOutlined,
  FolderOpenOutlined,
  LinkOutlined,
  SearchOutlined,
  WarningOutlined,
} from "@ant-design/icons";
import { Alert, Button, Empty } from "antd";
import { useEffect, useState } from "react";
import { useNavigate, useParams } from "react-router-dom";

import type { ArchiveObservation, ComponentDefectArchive, ComponentSummary } from "../api/componentArchiveApi";
import { fetchComponentArchive, fetchComponents } from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { ComponentDetailPanel } from "../archive/ComponentDetailPanel";
import { ComponentListPanel } from "../archive/ComponentListPanel";
import { RebindDialog } from "../archive/RebindDialog";
import { backendBaseUrl } from "../config";
import "./ComponentArchivePage.css";

interface ArchiveZeroStateProps {
  components: ComponentSummary[] | null;
  onSelect: (componentId: string) => void;
}

/**
 * 未选构件时的右侧面板。
 *
 * 这里原来摆着"导入检测资料"和"查看构件台账"两个按钮，但都不是这个页面该发起的动作：
 * 导入属于年度检测，台账是另一条业务线。删掉之后剩下大片空白，所以改成把**最该先看的
 * 构件**摆出来——这一页的用途就是找有问题的构件，空态直接给答案比让人自己去左栏翻更省事。
 */
function ArchiveZeroState({ components, onSelect }: ArchiveZeroStateProps) {
  const worst = (components ?? [])
    .filter((component): component is ComponentSummary & { latest_score: number } =>
      component.latest_score !== null)
    .sort((left, right) => left.latest_score - right.latest_score)
    .slice(0, 8);

  return (
    <div className="archive-zero-state">
      <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={false} />
      <h2>请选择一个构件</h2>
      <p>从左侧选择构件，查看历年病害、照片与跨年变化。</p>
      {worst.length > 0 ? (
        <section className="archive-zero-picks">
          <h3>评分最低的构件</h3>
          <div className="archive-zero-pick-list">
            {worst.map((component) => (
              <button
                key={component.id}
                type="button"
                className="archive-zero-pick"
                onClick={() => onSelect(component.id)}
              >
                <span className="archive-zero-pick-code">{component.business_component_code}</span>
                <span className="archive-zero-pick-part">{component.structure_part}</span>
                <span className="archive-zero-pick-score">
                  {Math.round((component.latest_score + Number.EPSILON) * 100) / 100}
                </span>
              </button>
            ))}
          </div>
        </section>
      ) : null}
      <aside>
        <LinkOutlined /> 跨年度病害会根据构件绑定与病害特征生成追踪线索
      </aside>
    </div>
  );
}

// 模块 06 构件病害档案主页面（A1 布局）：概况指标 + 整理提示 + 左侧构件列表 / 右侧档案详情。
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
    setArchiveError(null);
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
  const componentCount = components?.length ?? 0;
  const threadCount = components?.reduce((total, component) => total + component.thread_count, 0) ?? 0;
  const crossYearCount = components?.filter((component) => component.first_seen_year !== component.latest_seen_year).length ?? 0;
  const firstImportedYear = components?.length
    ? Math.min(...components.map((component) => component.first_seen_year))
    : null;
  const latestImportedYear = components?.length
    ? Math.max(...components.map((component) => component.latest_seen_year))
    : null;
  const importedYearCount = firstImportedYear !== null && latestImportedYear !== null
    ? latestImportedYear - firstImportedYear + 1
    : 0;

  return (
    <section className="archive-page archive-page-redesign">
      <section className="archive-metrics" aria-label="构件病害档案概况">
        {[
          { label: "病害构件", value: componentCount, tone: "blue", icon: <ApartmentOutlined /> },
          { label: "病害线索", value: threadCount, tone: "orange", icon: <WarningOutlined /> },
          { label: "跨年构件", value: crossYearCount, tone: "green", icon: <LinkOutlined /> },
          { label: "待整理观测", value: unboundCount, tone: "violet", icon: <FolderOpenOutlined /> },
        ].map((item) => (
          <article key={item.label} className={`archive-metric is-${item.tone}`}>
            <span>{item.icon}</span>
            <div>
              <small>{item.label}</small>
              <strong>{item.value}</strong>
            </div>
          </article>
        ))}
      </section>

      {unboundCount > 0 ? (
        <Alert
          className="archive-guidance-alert"
          type="info"
          showIcon
          title={`${importedYearCount} 个年度已导入，${unboundCount} 条病害观测尚未整理为跨年线索`}
          description="可先查看单个构件的年度记录，再进入线索整理批量确认。"
          action={
            <Button type="primary" href={`/bridges/${bridgeId}/defect-threads/triage`}>
              开始线索整理
            </Button>
          }
        />
      ) : null}

      <div className="archive-layout">
        <aside className="archive-filter-card">
          <header className="archive-list-title">
            <h2>病害构件</h2>
            <strong>{componentCount}</strong>
          </header>
          {listError ? (
            <p className="archive-empty-hint">{listError}</p>
          ) : components === null ? (
            <p>正在加载构件列表…</p>
          ) : components.length === 0 ? (
            <div className="archive-filter-empty">
              <SearchOutlined />
              <p>暂无病害构件</p>
              <span>导入并确认年度检测后，可在这里按构件筛选。</span>
            </div>
          ) : (
            <ComponentListPanel
              components={components}
              selectedComponentId={componentId ?? null}
              onSelect={(nextId) => navigate(`/bridges/${bridgeId}/components/${nextId}`)}
            />
          )}
        </aside>

        <section className="archive-detail-slot">
          {!componentId ? (
            <ArchiveZeroState
              components={components}
              onSelect={(nextId) => navigate(`/bridges/${bridgeId}/components/${nextId}`)}
            />
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
        </section>
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
