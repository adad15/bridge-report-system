import {
  CheckCircleOutlined,
  ClockCircleOutlined,
  FileWordOutlined,
  ImportOutlined,
  InboxOutlined,
  LinkOutlined,
  WarningOutlined,
} from "@ant-design/icons";
import { Steps } from "antd";
import { useEffect, useMemo, useState } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import { useAuth } from "../auth/AuthContext";
import { fetchInspectionYears, type InspectionYearSummary } from "../api/navigationApi";
import {
  dropCached,
  inspectionWorkspaceCacheKey,
  inspectionYearsCacheKey,
  readCached,
  writeCached,
} from "../api/resourceCache";
import { fetchInspectionWorkspace, type InspectionWorkspace, type WorkspaceImport } from "../api/workspaceApi";
import { backendBaseUrl } from "../config";
import { CreateInspectionDialog } from "../workspace/CreateInspectionDialog";
import { ImportWordDialog } from "../workspace/ImportWordDialog";
import { DeleteInspectionYearDialog } from "../workspace/DeleteInspectionYearDialog";
import { DeleteImportRecordDialog } from "../workspace/DeleteImportRecordDialog";
import { useBridgeWorkspace } from "../workspace/BridgeWorkspaceShell";
import { deriveInspectionProgress, inspectionWorkspacePath, reviewPath, statusBadgeClass } from "../workspace/workspaceState";

const actionLabel = (item: WorkspaceImport) => {
  if (item.available_action === "continue_review") return "继续校对";
  if (item.available_action === "view_result") return "查看结果";
  if (item.available_action === "reupload") return "重新上传";
  if (item.available_action === "parse") return item.import_status === "解析失败" ? "重新解析" : "开始解析";
  return null;
};

interface ImportDialogState {
  retry: WorkspaceImport | null;
  inspectionYearId: string;
  inspectionYear: number;
}

export function InspectionWorkspacePage() {
  const { bridgeId, inspectionYearId } = useParams<{ bridgeId: string; inspectionYearId?: string }>();
  const navigate = useNavigate();
  const { user } = useAuth();
  const { overview, reloadOverview } = useBridgeWorkspace();
  const [years, setYears] = useState<InspectionYearSummary[] | null>(null);
  const [workspace, setWorkspace] = useState<InspectionWorkspace | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [version, setVersion] = useState(0);
  const [showCreate, setShowCreate] = useState(false);
  const [importDialog, setImportDialog] = useState<ImportDialogState | null>(null);
  const [showDelete, setShowDelete] = useState(false);
  const [deleteImport, setDeleteImport] = useState<WorkspaceImport | null>(null);

  useEffect(() => {
    if (!bridgeId) return;
    let cancelled = false;
    // 有上次的结果就先渲染，再后台校验；切回页签时不再从空白开始。
    const cacheKey = inspectionYearsCacheKey(bridgeId);
    const cached = readCached<InspectionYearSummary[]>(cacheKey);
    if (cached) setYears(cached);
    fetchInspectionYears(backendBaseUrl, bridgeId)
      .then((items) => {
        const current = items
          .filter((item) => item.is_current)
          .sort((a, b) => b.inspection_year - a.inspection_year);
        writeCached(cacheKey, current);
        if (!cancelled) setYears(current);
      })
      .catch((caught) => {
        if (!cancelled) setError(caught instanceof ApiError ? caught.message : "年度列表加载失败。");
      });
    return () => { cancelled = true; };
  }, [bridgeId, version]);

  useEffect(() => {
    if (!bridgeId || inspectionYearId || years === null || years.length === 0) return;
    navigate(inspectionWorkspacePath(bridgeId, years[0].id), { replace: true });
  }, [bridgeId, inspectionYearId, navigate, years]);

  useEffect(() => {
    if (!inspectionYearId) {
      setWorkspace(null);
      return;
    }
    let cancelled = false;
    // 用缓存打底而不是清成 null：后者会让每次切回都先闪一次加载态。
    const cacheKey = inspectionWorkspaceCacheKey(inspectionYearId);
    setWorkspace(readCached<InspectionWorkspace>(cacheKey) ?? null);
    setError(null);
    fetchInspectionWorkspace(backendBaseUrl, inspectionYearId)
      .then((body) => {
        if (cancelled) return;
        if (body.bridge.id !== bridgeId) {
          setError("该年度不属于当前桥梁。");
          return;
        }
        writeCached(cacheKey, body);
        setWorkspace(body);
      })
      .catch((caught) => {
        if (!cancelled) setError(caught instanceof ApiError ? caught.message : "年度工作台加载失败。");
      });
    return () => { cancelled = true; };
  }, [bridgeId, inspectionYearId, version]);

  const selectedExists = useMemo(
    () => !inspectionYearId || years === null || years.some((item) => item.id === inspectionYearId),
    [inspectionYearId, years]
  );
  if (!bridgeId) return <p className="error-text">缺少桥梁标识。</p>;

  const refresh = () => {
    // 显式刷新（新建年度、导入、删除等）必须丢弃缓存：这些场景下先闪一下改动前的
    // 旧内容比多等一会儿更糟。页签切换不走这里，仍然享受缓存。
    dropCached(inspectionYearsCacheKey(bridgeId));
    if (inspectionYearId) dropCached(inspectionWorkspaceCacheKey(inspectionYearId));
    setVersion((current) => current + 1);
  };

  const refreshAll = () => {
    refresh();
    reloadOverview();
  };

  return (
    <div className="inspection-layout">
      <aside className="inspection-year-rail">
        <div className="rail-heading"><h2>检测年度</h2><button type="button" onClick={() => setShowCreate(true)}>＋ 新建</button></div>
        {years === null ? <p>加载年份…</p> : years.length === 0 ? <p className="empty-hint">暂无年度</p> : years.map((year) => (
          <Link key={year.id} className={year.id === inspectionYearId ? "year-link active" : "year-link"} to={inspectionWorkspacePath(bridgeId, year.id)}>
            <strong>{year.inspection_year}</strong><span>{year.status} · V{year.version_number}</span>
          </Link>
        ))}
      </aside>

      <section className="inspection-workspace-content">
        {error ? <div className="workspace-card"><h2>无法加载年度工作台</h2><p className="error-text">{error}</p><button type="button" onClick={refresh}>重新加载</button></div> : null}
        {!error && years?.length === 0 ? <div className="workspace-card empty-workspace"><h2>从新建年度检测开始</h2><p>年度创建后，可在这里导入 Word、解析并进入全屏校对。</p><button type="button" className="primary-button" onClick={() => setShowCreate(true)}>新建年度检测</button></div> : null}
        {!error && !selectedExists ? <div className="workspace-card"><p className="error-text">指定年度不在当前桥梁的有效年度列表中。</p></div> : null}
        {!error && inspectionYearId && selectedExists && workspace === null ? <div className="workspace-card"><p>正在加载年度资料…</p></div> : null}
        {!error && workspace ? <AnnualWorkspace
          workspace={workspace}
          bridgeId={bridgeId}
          onImport={() => setImportDialog({
            retry: null,
            inspectionYearId: workspace.inspection_year.id,
            inspectionYear: workspace.inspection_year.inspection_year,
          })}
          onRetry={(item) => setImportDialog({
            retry: item,
            inspectionYearId: workspace.inspection_year.id,
            inspectionYear: workspace.inspection_year.inspection_year,
          })}
          canDelete={user?.role === "admin"}
          onDelete={() => setShowDelete(true)}
          onDeleteImport={setDeleteImport}
        /> : null}
      </section>

      {showCreate ? <CreateInspectionDialog bridgeId={bridgeId} onClose={() => setShowCreate(false)} onCreated={(id) => {
        setShowCreate(false); refreshAll(); navigate(inspectionWorkspacePath(bridgeId, id));
      }} /> : null}
      {importDialog ? <ImportWordDialog
        bridgeName={overview.bridge.bridge_name}
        inspectionYearId={importDialog.inspectionYearId}
        inspectionYear={importDialog.inspectionYear}
        retryImport={importDialog.retry}
        onClose={() => setImportDialog(null)}
        onChanged={refresh}
        onCompleted={(importId) => {
          reloadOverview();
          navigate(reviewPath(bridgeId, importDialog.inspectionYearId, importId));
        }}
      /> : null}
      {showDelete && workspace ? <DeleteInspectionYearDialog
        inspectionYearId={workspace.inspection_year.id}
        onClose={() => setShowDelete(false)}
        onDeleted={(result) => {
          setShowDelete(false);
          refreshAll();
          navigate(result.next_inspection_year_id
            ? inspectionWorkspacePath(bridgeId, result.next_inspection_year_id)
            : `/bridges/${encodeURIComponent(bridgeId)}/inspections`, { replace: true });
        }}
      /> : null}
      {deleteImport ? <DeleteImportRecordDialog
        importRecordId={deleteImport.id}
        onClose={() => setDeleteImport(null)}
        onDeleted={() => {
          setDeleteImport(null);
          refreshAll();
        }}
      /> : null}
    </div>
  );
}

function AnnualWorkspace({ workspace, bridgeId, onImport, onRetry, canDelete, onDelete, onDeleteImport }: {
  workspace: InspectionWorkspace;
  bridgeId: string;
  onImport: () => void;
  onRetry: (item: WorkspaceImport) => void;
  canDelete: boolean;
  onDelete: () => void;
  onDeleteImport: (item: WorkspaceImport) => void;
}) {
  const progress = deriveInspectionProgress(workspace.inspection_year, workspace.imports);
  const progressIndex: number = progress.stage === "completed" ? 4 : progress.stage === "review" ? 2 : 1;
  const pendingItems = workspace.imports.reduce((total, item) => total + item.statistics.pending_count, 0);
  const errorCount = workspace.imports.filter((item) => item.import_status === "解析失败").length;
  return (
    <>
      <section className="workspace-card annual-heading annual-workflow-card">
        <div className="annual-heading-main">
          <p className="section-kicker">{workspace.inspection_year.system_number}</p>
          <div className="annual-title-line">
            <h2>{workspace.inspection_year.inspection_year} 年度检测</h2>
            <span className={`progress-badge progress-${progress.stage}`}>{progress.label}</span>
            <span>当前版本 V{workspace.inspection_year.version_number}</span>
          </div>
        </div>
        <div className="annual-actions">{workspace.inspection_year.is_current ? <button className="primary-button" type="button" onClick={onImport}>导入资料</button> : null}{canDelete ? <details className="more-actions"><summary>更多</summary><div><button type="button" className="danger-menu-item" onClick={onDelete}>删除年度</button></div></details> : null}</div>
        <Steps
          className="annual-progress-steps"
          current={progressIndex}
          responsive={false}
          titlePlacement="vertical"
          items={[
            { title: "创建检测", content: "已完成" },
            { title: "导入资料", content: progressIndex === 1 ? "进行中" : "已完成" },
            { title: "数据校对", content: progressIndex === 2 ? "进行中" : progressIndex > 2 ? "已完成" : "待开始" },
            { title: "系统评定", content: progressIndex === 3 ? "进行中" : progressIndex > 3 ? "已完成" : "待开始" },
            { title: "生成报告", content: progressIndex === 4 ? "已完成" : "待开始" },
          ]}
        />
        {workspace.standard_profile ? <div className="inspection-standard-summary"><span><CheckCircleOutlined /> {workspace.standard_profile.technical_condition.standard_code} · 规则包 {workspace.standard_profile.technical_condition.package_version}（{workspace.standard_profile.technical_condition.is_enabled ? workspace.standard_profile.technical_condition.sync_status : "已停用"}）</span><span><CheckCircleOutlined /> {workspace.standard_profile.maintenance.standard_code} · 规则包 {workspace.standard_profile.maintenance.package_version}（{workspace.standard_profile.maintenance.is_enabled ? workspace.standard_profile.maintenance.sync_status : "已停用"}）</span></div> : <p className="warning-text">历史年度未绑定规范组合。</p>}
      </section>

      <section className="annual-metrics" aria-label="年度检测概况">
        {[
          { label: "导入记录", value: workspace.imports.length, icon: <ImportOutlined />, tone: "blue" },
          { label: "待绑定项", value: workspace.pending.unbound_observation_count, icon: <LinkOutlined />, tone: "orange" },
          { label: "待校对项", value: pendingItems, icon: <CheckCircleOutlined />, tone: "violet" },
          { label: "异常提醒", value: errorCount, icon: <WarningOutlined />, tone: "red" },
        ].map((item) => <article key={item.label} className={`annual-metric is-${item.tone}`}><span>{item.icon}</span><div><small>{item.label}</small><strong>{item.value}</strong></div></article>)}
      </section>

      <div className="annual-record-layout">
        <section className="workspace-card annual-record-card">
          {/* 空态下 kicker 与标题原本都是「资料与处理记录」，同一句话印两遍。
              有记录时 kicker 作分类、标题给条数；没有记录时标题自己就是分类。 */}
          <div className="card-heading"><div>
            {workspace.imports.length > 0 ? <p className="section-kicker">资料与处理记录</p> : null}
            <h2>{workspace.imports.length > 0 ? `${workspace.imports.length} 条导入记录` : "资料与处理记录"}</h2>
          </div></div>
        {workspace.imports.length === 0 ? <div className="annual-upload-empty">
            {/* 图标不再用 Word：来源已经有三种，点名其中一种会误导。 */}
            <span className="annual-upload-empty-icon" aria-hidden="true"><InboxOutlined /></span>
            <h3>尚未导入检测资料</h3>
            <p>导入后可进行构件绑定与数据校对。</p>
            {/* 三种来源摊开成一行三项：哪个能用、哪个还没做，一眼看完，
                比塞进一句长句里让人自己挑要快。 */}
            <ul className="annual-upload-sources">
              <li>
                <strong>博试云桥隧定检系统</strong>
                <small>读取本机离线库</small>
              </li>
              <li>
                <strong>Word 检测资料</strong>
                <small>软件导出 Word 或正式报告</small>
              </li>
              <li className="is-pending">
                <strong>移动端现场采集</strong>
                <small>开发中</small>
              </li>
            </ul>
            <div className="annual-upload-empty-actions">
              <button className="primary-button" type="button" onClick={onImport}>导入检测资料</button>
              <a href="#annual-next-step">查看导入说明</a>
            </div>
          </div> : (
          <div className="import-card-list">{workspace.imports.map((item) => {
            const label = actionLabel(item);
            const lockText = item.edit_lock ? `${item.edit_lock.owner_display_name} 正在编辑` : null;
            const importedAt = item.created_at ? new Date(item.created_at).toLocaleString("zh-CN", { hour12: false }) : "时间未知";
            return <article className="import-source-card annual-import-record" key={item.id}>
              <span className="annual-import-file-icon" aria-hidden="true"><FileWordOutlined /></span>
              <div className="annual-import-record-body">
                <div className="import-title-row"><h3>{item.import_name}</h3><span className={statusBadgeClass(item.import_status)}>{item.import_status}</span></div>
                <div className="annual-import-meta"><span>{item.system_number}</span><span>{item.source_type}</span><span><ClockCircleOutlined /> {importedAt}</span>{item.importer_name ? <span>导入人：{item.importer_name}</span> : null}</div>
                <div className="annual-import-statistics" aria-label={`${item.import_name} 处理统计`}>
                  <span><small>病害</small><strong>{item.statistics.defect_count}</strong></span>
                  <span><small>照片</small><strong>{item.statistics.photo_count}</strong></span>
                  <span><small>待校对</small><strong>{item.statistics.pending_count}</strong></span>
                  <span><small>已确认</small><strong>{item.statistics.confirmed_count}</strong></span>
                </div>
                {item.import_status === "解析失败" && item.error_message ? <p className="error-text">解析失败：{item.error_message}</p> : null}{item.import_status === "解析失败" && item.temporary_source_expires_at ? <p className="muted-text">临时 Word 保留至 {new Date(item.temporary_source_expires_at).toLocaleString()}</p> : null}{item.available_action === "reupload" ? <p className="error-text">原临时 Word 已不可用，请重新上传。</p> : null}{lockText ? <p className="lock-note">{lockText}</p> : null}
              </div>
              <div className="import-card-actions">
                {label ? item.available_action === "parse" ? <button className="primary-button" type="button" onClick={() => onRetry(item)}>{label}</button> : item.available_action === "reupload" ? <button className="primary-button" type="button" onClick={onImport}>{label}</button> : <Link className="annual-import-primary-link" to={reviewPath(bridgeId, workspace.inspection_year.id, item.id)}>{label}</Link> : <span className="muted-text">处理中</span>}
                {/* 删除原本和"继续校对"并排同权。破坏性操作不该走主流程视觉，收进"更多"，
                    和上面年度卡删除年度的做法保持一致。一页可能有多张导入卡，summary 要
                    带上记录名才区分得开。 */}
                {canDelete ? (
                  <details className="more-actions">
                    <summary aria-label={`更多操作 ${item.import_name}`}>更多</summary>
                    <div>
                      <button type="button" className="danger-menu-item" onClick={() => onDeleteImport(item)}>删除导入记录</button>
                    </div>
                  </details>
                ) : null}
              </div>
            </article>;
          })}</div>
        )}
        </section>
        <aside id="annual-next-step" className="workspace-card annual-next-card">
          <h2>下一步</h2>
          <ol>
            <li className={progressIndex === 1 ? "is-active" : ""}><span>2</span><div><strong>导入资料</strong><p>导入 Word 检测资料，系统将自动解析内容。</p></div></li>
            <li className={progressIndex === 2 ? "is-active" : ""}><span>3</span><div><strong>数据校对</strong><p>对识别的数据进行校对，确认构件与指标信息。</p></div></li>
            <li><span>4</span><div><strong>系统评定</strong><p>完成校对后，系统将执行评定并生成结果。</p></div></li>
          </ol>
        </aside>
      </div>
    </>
  );
}
