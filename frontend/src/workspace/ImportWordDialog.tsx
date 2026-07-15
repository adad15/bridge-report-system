import { type FormEvent, useState } from "react";

import { parseWordImport } from "../api/reviewApi";
import { type WorkspaceImport, uploadWordImport, workspaceErrorMessage } from "../api/workspaceApi";
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

export function ImportWordDialog({
  bridgeName,
  inspectionYearId,
  inspectionYear,
  retryImport,
  onClose,
  onChanged,
  onCompleted,
}: Props) {
  const [file, setFile] = useState<File | null>(null);
  const [sourceType, setSourceType] = useState<"软件导出Word" | "正式Word">("软件导出Word");
  const [inspectionDate, setInspectionDate] = useState("");
  const [reportNumber, setReportNumber] = useState("");
  const [projectName, setProjectName] = useState(`${bridgeName}${inspectionYear}年度定期检测`);
  const [phase, setPhase] = useState<"idle" | "uploading" | "parsing">("idle");
  const [error, setError] = useState<string | null>(null);

  const busy = phase !== "idle";

  async function submit(event: FormEvent) {
    event.preventDefault();
    setError(null);
    if (!retryImport) {
      if (!file || !file.name.toLocaleLowerCase().endsWith(".docx")) {
        setError("请选择一个 .docx 文件。");
        return;
      }
    }
    if (!inspectionDate || !reportNumber.trim() || !projectName.trim()) {
      setError("检查日期、报告编号和项目名称均为必填项。");
      return;
    }

    try {
      let importRecordId = retryImport?.id;
      if (!importRecordId) {
        setPhase("uploading");
        const uploaded = await uploadWordImport(backendBaseUrl, inspectionYearId, file!, sourceType);
        importRecordId = uploaded.id;
        onChanged();
      }
      setPhase("parsing");
      await parseWordImport(backendBaseUrl, importRecordId, {
        rule_profile: "辽宁国省干线",
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
      onChanged();
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="workspace-dialog import-dialog" role="dialog" aria-modal="true" aria-labelledby="import-word-title" onSubmit={(event) => void submit(event)}>
        <h2 id="import-word-title">{retryImport ? "重新解析 Word 资料" : "导入 Word 资料"}</h2>
        {retryImport ? <p className="dialog-note">将重新解析已归档文件：{retryImport.import_name}</p> : (
          <>
            <label>Word 文件<input type="file" accept=".docx,application/vnd.openxmlformats-officedocument.wordprocessingml.document" onChange={(event) => setFile(event.target.files?.[0] ?? null)} /></label>
            <label>来源类型<select value={sourceType} onChange={(event) => setSourceType(event.target.value as typeof sourceType)}><option>软件导出Word</option><option>正式Word</option></select></label>
          </>
        )}
        <label>规则模板<input value="辽宁国省干线" disabled /></label>
        <label>检查日期<input type="date" required value={inspectionDate} onChange={(event) => setInspectionDate(event.target.value)} /></label>
        <label>报告编号<input required value={reportNumber} onChange={(event) => setReportNumber(event.target.value)} /></label>
        <label>项目名称<input required value={projectName} onChange={(event) => setProjectName(event.target.value)} /></label>
        {phase === "uploading" ? <p className="progress-text">正在上传并归档 Word…</p> : null}
        {phase === "parsing" ? <p className="progress-text">正在解析病害、照片和评分…</p> : null}
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="dialog-actions"><button type="button" onClick={onClose} disabled={busy}>取消</button><button className="primary-button" type="submit" disabled={busy}>{busy ? "处理中…" : retryImport ? "重新解析" : "上传并解析"}</button></div>
      </form>
    </div>
  );
}
