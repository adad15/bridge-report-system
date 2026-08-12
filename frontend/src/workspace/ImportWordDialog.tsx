import { type DragEvent, type FormEvent, useCallback, useEffect, useRef, useState } from "react";

import { parseWordImport } from "../api/reviewApi";
import {
  type SourceTaskRow,
  type WorkspaceImport,
  createSourceDbImport,
  listSourceTasks,
  uploadWordImport,
  workspaceErrorMessage,
} from "../api/workspaceApi";
import { backendBaseUrl } from "../config";

interface Props {
  bridgeName: string;
  inspectionYearId: string;
  inspectionYear: number;
  retryImport?: WorkspaceImport | null;
  onClose: () => void;
  onChanged: () => void;
  onCompleted: (importRecordId: string) => void;
}

/** 导入列表里认得出来的名字：桥名 + 检测日期。 */
function sourceTaskLabel(task?: SourceTaskRow): string | undefined {
  if (!task) return undefined;
  return [task.name, task.check_date ?? "未填日期"].filter(Boolean).join(" ");
}

function formatFileSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${Math.round(bytes / 1024)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

export function ImportWordDialog({
  bridgeName,
  inspectionYearId,
  inspectionYear,
  retryImport,
  onClose,
  onChanged,
  onCompleted,
}: Props) {
  const [dataSource, setDataSource] = useState<"来源软件" | "Word">("来源软件");
  const [sourceDbPath, setSourceDbPath] = useState("");
  const [taskId, setTaskId] = useState("");
  const [tasks, setTasks] = useState<SourceTaskRow[] | null>(null);
  const [loadingTasks, setLoadingTasks] = useState(false);
  const [taskError, setTaskError] = useState<string | null>(null);
  const [file, setFile] = useState<File | null>(null);
  const [dragging, setDragging] = useState(false);
  const [sourceType, setSourceType] = useState<"软件导出Word" | "正式Word">("软件导出Word");
  const [inspectionDate, setInspectionDate] = useState("");
  const [reportNumber, setReportNumber] = useState("");
  const [projectName, setProjectName] = useState(`${bridgeName}${inspectionYear}年度定期检测`);
  const [phase, setPhase] = useState<"idle" | "uploading" | "parsing">("idle");
  const [error, setError] = useState<string | null>(null);
  const sourceDbPathRef = useRef<HTMLInputElement>(null);
  const taskIdRef = useRef<HTMLSelectElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const inspectionDateRef = useRef<HTMLInputElement>(null);
  const reportNumberRef = useRef<HTMLInputElement>(null);
  const projectNameRef = useRef<HTMLInputElement>(null);

  const busy = phase !== "idle";
  // 重新解析走的是既有导入记录，来源在登记时就定了，这里不再让人改。
  const fromSourceDb = !retryImport && dataSource === "来源软件";

  const loadTasks = useCallback(async (path?: string) => {
    setLoadingTasks(true);
    setTaskError(null);
    try {
      const found = await listSourceTasks(backendBaseUrl, path);
      setSourceDbPath(found.source_db_path);
      setTasks(found.tasks);
      setTaskId(found.tasks.length === 1 ? found.tasks[0].task_id : "");
    } catch (caught) {
      setTasks(null);
      setTaskError(workspaceErrorMessage(caught));
    } finally {
      setLoadingTasks(false);
    }
  }, []);

  // 一打开就去读默认位置的离线库：绝大多数情况下用户什么都不用填。
  useEffect(() => {
    if (fromSourceDb && tasks === null && !loadingTasks && taskError === null) {
      void loadTasks();
    }
  }, [fromSourceDb, tasks, loadingTasks, taskError, loadTasks]);
  // 选错扩展名当场就说，不必等到点了"上传并解析"才发现——但拦截仍在提交处，
  // 这里只是提前把红框亮出来。
  const wrongExtension = !!file && !file.name.toLocaleLowerCase().endsWith(".docx");

  function acceptDrop(event: DragEvent<HTMLDivElement>) {
    event.preventDefault();
    setDragging(false);
    if (busy) return;
    const dropped = event.dataTransfer.files?.[0];
    if (dropped) setFile(dropped);
  }

  async function submit(event: FormEvent) {
    event.preventDefault();
    setError(null);
    if (fromSourceDb) {
      if (!sourceDbPath.trim()) {
        setError("请填写来源软件离线库的完整路径。");
        sourceDbPathRef.current?.focus();
        return;
      }
      if (!taskId.trim()) {
        setError("请选择要导入的检测任务。");
        taskIdRef.current?.focus();
        return;
      }
    } else if (!retryImport) {
      if (!file || !file.name.toLocaleLowerCase().endsWith(".docx")) {
        setError("请选择一个 .docx 文件。");
        fileInputRef.current?.focus();
        return;
      }
    }
    if (!inspectionDate) {
      setError("请选择检查日期。");
      inspectionDateRef.current?.focus();
      return;
    }
    if (!reportNumber.trim()) {
      setError("请填写报告编号。");
      reportNumberRef.current?.focus();
      return;
    }
    if (!projectName.trim()) {
      setError("请填写项目名称。");
      projectNameRef.current?.focus();
      return;
    }

    try {
      let importRecordId = retryImport?.id;
      if (!importRecordId) {
        setPhase("uploading");
        const created = fromSourceDb
          ? await createSourceDbImport(
              backendBaseUrl, inspectionYearId, sourceDbPath.trim(), taskId.trim(),
              sourceTaskLabel(tasks?.find((task) => task.task_id === taskId)))
          : await uploadWordImport(backendBaseUrl, inspectionYearId, file!, sourceType);
        importRecordId = created.id;
      }
      setPhase("parsing");
      await parseWordImport(backendBaseUrl, importRecordId, {
        // 源库那条路没有 Word 规则档；后端按导入记录的来源决定调哪个解析端点。
        ...(fromSourceDb ? {} : { rule_profile: "辽宁国省干线" }),
        import_mode: "已有桥年度导入",
        file_role: "当前年度检测资料",
        data_role: "当前年度",
        inspection_date: inspectionDate,
        report_number: reportNumber.trim(),
        project_name: projectName.trim(),
      });
      onChanged();
      onCompleted(importRecordId);
    } catch (caught) {
      setPhase("idle");
      setError(workspaceErrorMessage(caught));
      // 后端会自动删除解析失败的导入记录；立即刷新工作台，避免底层列表
      // 继续显示提交开始时创建的短暂记录。
      onChanged();
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form
        className="workspace-dialog import-dialog"
        role="dialog"
        aria-modal="true"
        aria-labelledby="import-word-title"
        noValidate
        onSubmit={(event) => void submit(event)}
      >
        <header className="import-dialog-header">
          <h2 id="import-word-title">{retryImport ? "重新解析检测资料" : "导入检测资料"}</h2>
          {/* 弹窗一开就盖住了页面，得自己说清楚这份资料要落到哪座桥的哪一年；
              规则模板恒为辽宁国省干线，跟着落在同一行，不值得单占一个字段。 */}
          <p className="import-dialog-target">
            {bridgeName} · {inspectionYear} 年度
            {fromSourceDb ? null : (
              <span className="import-dialog-profile">规则模板 辽宁国省干线</span>
            )}
          </p>
        </header>

        {retryImport ? (
          <p className="dialog-note">将重新解析仍在保留期内的临时资料：{retryImport.import_name}</p>
        ) : (
          <>
            <div className="import-field">
              <label htmlFor="import-data-source">数据来源</label>
              <select
                id="import-data-source"
                value={dataSource}
                disabled={busy}
                onChange={(event) => setDataSource(event.target.value as typeof dataSource)}
              >
                <option value="来源软件">来源软件离线库</option>
                <option value="Word">Word 文件</option>
              </select>
            </div>
          </>
        )}

        {fromSourceDb ? (
          <>
            <p className="dialog-note">
              请先在来源软件的桌面程序里打开该桥并下载对应年度——数据要在那一步才会落到本机离线库里。
            </p>
            <div className="import-field required">
              <label htmlFor="import-source-db-path">离线库路径</label>
              <div className="import-source-path-row">
                <input
                  id="import-source-db-path"
                  ref={sourceDbPathRef}
                  required
                  disabled={busy || loadingTasks}
                  value={sourceDbPath}
                  placeholder={loadingTasks ? "正在查找来源软件的离线库…" : "离线库的完整路径"}
                  onChange={(event) => setSourceDbPath(event.target.value)}
                />
                <button
                  type="button"
                  disabled={busy || loadingTasks}
                  onClick={() => void loadTasks(sourceDbPath.trim() || undefined)}
                >
                  {loadingTasks ? "读取中…" : "重新读取"}
                </button>
              </div>
              <p className="import-field-hint">
                已按来源软件的默认位置填好，一般不用改。离线库只会被只读打开，不会被复制或修改。
              </p>
              {taskError ? <p className="import-field-warn">{taskError}</p> : null}
            </div>
            <div className="import-field required">
              <label htmlFor="import-source-task">检测任务</label>
              <select
                id="import-source-task"
                ref={taskIdRef}
                required
                disabled={busy || loadingTasks || !tasks?.length}
                value={taskId}
                onChange={(event) => setTaskId(event.target.value)}
              >
                <option value="">
                  {loadingTasks ? "正在读取…" : tasks?.length ? "请选择" : "未读到检测任务"}
                </option>
                {(tasks ?? []).map((task) => (
                  <option key={task.task_id} value={task.task_id}>
                    {[task.name, task.check_date ?? "未填日期",
                      `${task.defect_count} 条病害`, `${task.photo_count} 张照片`].join(" · ")}
                  </option>
                ))}
              </select>
              <p className="import-field-hint">
                一份离线库里存着你打开过的所有桥；病害与照片条数可以用来确认这一年是否已下载全。
              </p>
            </div>
          </>
        ) : null}

        {retryImport || fromSourceDb ? null : (
          <>
            <div className="import-field required">
              <label htmlFor="import-word-file">Word 文件</label>
              <div
                className={[
                  "import-file-picker",
                  file ? "has-file" : "",
                  dragging ? "dragging" : "",
                  wrongExtension ? "invalid" : "",
                ].filter(Boolean).join(" ")}
                onDragOver={(event) => {
                  event.preventDefault();
                  if (!busy) setDragging(true);
                }}
                onDragLeave={() => setDragging(false)}
                onDrop={acceptDrop}
              >
                <input
                  id="import-word-file"
                  ref={fileInputRef}
                  className="import-file-input"
                  type="file"
                  accept=".docx,application/vnd.openxmlformats-officedocument.wordprocessingml.document"
                  disabled={busy}
                  onChange={(event) => setFile(event.target.files?.[0] ?? null)}
                />
                <span className="import-file-body" aria-hidden="true">
                  {file ? (
                    <>
                      <span className="import-file-name">{file.name}</span>
                      <span className="import-file-meta">{formatFileSize(file.size)} · 点击可重选</span>
                    </>
                  ) : (
                    <>
                      <span className="import-file-name">点击选择，或把 .docx 拖到这里</span>
                      <span className="import-file-meta">仅支持检测软件导出的 .docx</span>
                    </>
                  )}
                </span>
              </div>
              {wrongExtension ? <p className="import-field-warn">只能导入 .docx 文件。</p> : null}
            </div>

            <div className="import-field">
              <label htmlFor="import-source-type">来源类型</label>
              <select
                id="import-source-type"
                value={sourceType}
                disabled={busy}
                onChange={(event) => setSourceType(event.target.value as typeof sourceType)}
              >
                <option>软件导出Word</option>
                <option>正式Word</option>
              </select>
            </div>
          </>
        )}

        <div className="import-field-row">
          <div className="import-field required">
            <label htmlFor="import-inspection-date">检查日期</label>
            <input
              id="import-inspection-date"
              ref={inspectionDateRef}
              type="date"
              required
              disabled={busy}
              value={inspectionDate}
              onChange={(event) => setInspectionDate(event.target.value)}
            />
          </div>
          <div className="import-field required">
            <label htmlFor="import-report-number">报告编号</label>
            <input
              id="import-report-number"
              ref={reportNumberRef}
              required
              disabled={busy}
              value={reportNumber}
              onChange={(event) => setReportNumber(event.target.value)}
            />
          </div>
        </div>

        <div className="import-field required">
          <label htmlFor="import-project-name">项目名称</label>
          <input
            id="import-project-name"
            ref={projectNameRef}
            required
            disabled={busy}
            value={projectName}
            onChange={(event) => setProjectName(event.target.value)}
          />
          <p className="import-field-hint">已按桥名与年度预填，如与报告封面不一致请改成封面上的写法。</p>
        </div>

        {phase === "uploading" ? (
          <p className="progress-text">
            {fromSourceDb ? "正在登记来源库导入…" : "正在上传并临时保存 Word…"}
          </p>
        ) : null}
        {phase === "parsing" ? <p className="progress-text">正在解析病害、照片和评分…</p> : null}
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="dialog-actions">
          <button type="button" onClick={onClose} disabled={busy}>取消</button>
          <button className="primary-button" type="submit" disabled={busy}>
            {busy ? "处理中…" : retryImport ? "重新解析" : fromSourceDb ? "开始导入" : "上传并解析"}
          </button>
        </div>
      </form>
    </div>
  );
}
