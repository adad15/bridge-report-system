import { useEffect, useMemo, useState } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import { useAuth } from "../auth/AuthContext";
import { fetchInspectionYears, type InspectionYearSummary } from "../api/navigationApi";
import { fetchInspectionWorkspace, type InspectionWorkspace, type WorkspaceImport } from "../api/workspaceApi";
import { backendBaseUrl } from "../config";
import { CreateInspectionDialog } from "../workspace/CreateInspectionDialog";
import { ImportWordDialog } from "../workspace/ImportWordDialog";
import { DeleteInspectionYearDialog } from "../workspace/DeleteInspectionYearDialog";
import { useBridgeWorkspace } from "../workspace/BridgeWorkspaceShell";
import { deriveInspectionProgress, inspectionWorkspacePath, reviewPath } from "../workspace/workspaceState";

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

  useEffect(() => {
    if (!bridgeId) return;
    let cancelled = false;
    fetchInspectionYears(backendBaseUrl, bridgeId)
      .then((items) => {
        if (!cancelled) setYears(items.filter((item) => item.is_current).sort((a, b) => b.inspection_year - a.inspection_year));
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
    setWorkspace(null);
    setError(null);
    fetchInspectionWorkspace(backendBaseUrl, inspectionYearId)
      .then((body) => {
        if (cancelled) return;
        if (body.bridge.id !== bridgeId) {
          setError("该年度不属于当前桥梁。");
          return;
        }
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
    setVersion((current) => current + 1);
  };

  const refreshAll = () => {
    refresh();
    reloadOverview();
  };

  return (
    <div className="inspection-layout">
      <aside className="inspection-year-rail">
        <div className="rail-heading"><h2>年度检测</h2><button type="button" onClick={() => setShowCreate(true)}>＋ 新建</button></div>
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
    </div>
  );
}

function AnnualWorkspace({ workspace, bridgeId, onImport, onRetry, canDelete, onDelete }: {
  workspace: InspectionWorkspace;
  bridgeId: string;
  onImport: () => void;
  onRetry: (item: WorkspaceImport) => void;
  canDelete: boolean;
  onDelete: () => void;
}) {
  const progress = deriveInspectionProgress(workspace.inspection_year, workspace.imports);
  return (
    <>
      <section className="workspace-card annual-heading">
        <div><p className="section-kicker">{workspace.inspection_year.system_number}</p><h2>{workspace.inspection_year.inspection_year} 年度检测</h2><p>{workspace.inspection_year.status} · 当前版本 V{workspace.inspection_year.version_number}</p></div>
        <div className="annual-actions"><span className={`progress-badge progress-${progress.stage}`}>{progress.label}</span>{workspace.inspection_year.is_current ? <button className="primary-button" type="button" onClick={onImport}>导入资料</button> : null}{canDelete ? <details className="more-actions"><summary>更多</summary><div><button type="button" className="danger-menu-item" onClick={onDelete}>删除年度</button></div></details> : null}</div>
      </section>
      <section className="workspace-card">
        <div className="card-heading"><div><p className="section-kicker">资料与处理记录</p><h2>{workspace.imports.length} 条导入记录</h2></div></div>
        {workspace.imports.length === 0 ? <p className="empty-hint">尚未导入资料。当前仅支持 Word，后续可扩展其他格式。</p> : (
          <div className="import-card-list">{workspace.imports.map((item) => {
            const label = actionLabel(item);
            const lockText = item.edit_lock ? `${item.edit_lock.owner_display_name} 正在编辑` : null;
            return <article className="import-source-card" key={item.id}>
              <div><div className="import-title-row"><h3>{item.import_name}</h3><span className="status-badge">{item.import_status}</span></div><p>{item.system_number} · {item.source_type}</p><p>病害 {item.statistics.defect_count} · 照片 {item.statistics.photo_count} · 评分 {item.statistics.rating_item_count}</p>{item.import_status === "解析失败" && item.error_message ? <p className="error-text">解析失败：{item.error_message}</p> : null}{item.import_status === "解析失败" && item.temporary_source_expires_at ? <p className="muted-text">临时 Word 保留至 {new Date(item.temporary_source_expires_at).toLocaleString()}</p> : null}{item.available_action === "reupload" ? <p className="error-text">原临时 Word 已不可用，请重新上传。</p> : null}{lockText ? <p className="lock-note">{lockText}</p> : null}</div>
              {label ? item.available_action === "parse" ? <button type="button" onClick={() => onRetry(item)}>{label}</button> : item.available_action === "reupload" ? <button type="button" onClick={onImport}>{label}</button> : <Link to={reviewPath(bridgeId, workspace.inspection_year.id, item.id)}>{label}</Link> : <span className="muted-text">处理中</span>}
            </article>;
          })}</div>
        )}
      </section>
    </>
  );
}
