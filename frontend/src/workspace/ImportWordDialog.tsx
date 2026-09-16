import { InboxOutlined } from "@ant-design/icons";
import {
  Alert,
  Button,
  Col,
  DatePicker,
  Flex,
  Form,
  Input,
  Modal,
  Row,
  Select,
  Tag,
  Typography,
  Upload,
  type GetRef,
  type InputRef,
  type RefSelectProps,
} from "antd";
import dayjs from "dayjs";
import { useCallback, useEffect, useRef, useState } from "react";

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

/** 导入列表里认得出来的名字：桥名 + 检测日期。
 *  这个串会作为 import_name 落库，所以只放事实、不放状态词——原先没日期时会拼进
 *  "未填日期"，存下来之后就永远长在记录名里，列表上读起来像桥名的一部分。缺日期时
 *  留空即可，页面另有位置提示。 */
function sourceTaskLabel(task?: SourceTaskRow): string | undefined {
  if (!task) return undefined;
  return [task.name, task.check_date].filter(Boolean).join(" ") || undefined;
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
  const [sourceType, setSourceType] = useState<"软件导出Word" | "正式Word">("软件导出Word");
  const [inspectionDate, setInspectionDate] = useState("");
  const [reportNumber, setReportNumber] = useState("");
  const [projectName, setProjectName] = useState(`${bridgeName}${inspectionYear}年度定期检测`);
  const [phase, setPhase] = useState<"idle" | "uploading" | "parsing">("idle");
  const [error, setError] = useState<string | null>(null);
  const sourceDbPathRef = useRef<InputRef>(null);
  const taskIdRef = useRef<RefSelectProps>(null);
  const inspectionDateRef = useRef<GetRef<typeof DatePicker>>(null);
  const reportNumberRef = useRef<InputRef>(null);
  const projectNameRef = useRef<InputRef>(null);

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

  async function submit() {
    setError(null);
    if (fromSourceDb) {
      if (!sourceDbPath.trim()) {
        setError("请填写博试云桥隧定检系统离线库的完整路径。");
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
        return;
      }
    }
    if (inspectionDate === "") {
      setError("请选择检查日期，格式为 2026-05-18。");
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

  const primaryText = retryImport ? "重新解析" : fromSourceDb ? "开始导入" : "上传并解析";

  return (
    <Modal
      open
      centered
      width={640}
      mask={{ closable: false }}
      onCancel={onClose}
      title={retryImport ? "重新解析检测资料" : "导入检测资料"}
      styles={{ body: { maxHeight: "calc(100vh - 220px)", overflowY: "auto", overflowX: "hidden" } }}
      footer={[
        <Button key="cancel" onClick={onClose} disabled={busy}>取消</Button>,
        <Button key="submit" type="primary" loading={busy} onClick={() => void submit()}>{primaryText}</Button>,
      ]}
    >
      <Flex vertical gap={12}>
        {/* 弹窗一开就盖住了页面，得自己说清楚这份资料要落到哪座桥的哪一年；
            规则模板恒为辽宁国省干线，跟着落在同一行，不值得单占一个字段。 */}
        <Flex align="center" gap={8} wrap>
          <Typography.Text type="secondary">{bridgeName} · {inspectionYear} 年度</Typography.Text>
          {fromSourceDb ? null : <Tag>规则模板 辽宁国省干线</Tag>}
        </Flex>

        {retryImport ? (
          <Alert type="info" showIcon role="note" title={`将重新解析仍在保留期内的临时资料：${retryImport.import_name}`} />
        ) : null}

        <Form layout="vertical" onFinish={() => void submit()}>
          {retryImport ? null : (
            <Form.Item label="数据来源" htmlFor="import-data-source">
              <Select
                id="import-data-source"
                value={dataSource}
                disabled={busy}
                onChange={setDataSource}
                // value 是本地分支用的判别值，不发给后端；这里只改显示名。
                options={[
                  { value: "来源软件", label: "博试云桥隧定检系统" },
                  { value: "Word", label: "Word 文件" },
                ]}
              />
            </Form.Item>
          )}

          {/* 检查日期排在来源相关字段之前：它的浮层向下展开，放在表单末尾容易被弹窗底边挡住。 */}
          <Row gutter={14}>
            <Col span={12}>
              <Form.Item label="检查日期" htmlFor="import-inspection-date" required>
                <DatePicker
                  id="import-inspection-date"
                  ref={inspectionDateRef}
                  format="YYYY-MM-DD"
                  placeholder="2026-05-18"
                  disabled={busy}
                  value={inspectionDate ? dayjs(inspectionDate) : null}
                  onChange={(date) => setInspectionDate(date ? date.format("YYYY-MM-DD") : "")}
                  style={{ width: "100%" }}
                />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item label="报告编号" htmlFor="import-report-number" required>
                <Input
                  id="import-report-number"
                  ref={reportNumberRef}
                  disabled={busy}
                  value={reportNumber}
                  onChange={(event) => setReportNumber(event.target.value)}
                />
              </Form.Item>
            </Col>
          </Row>

          {fromSourceDb ? (
            <>
              <Form.Item>
                <Alert
                  type="info"
                  showIcon
                  role="note"
                  title="请先在博试云桥隧定检系统的桌面程序里打开该桥并下载对应年度——数据要在那一步才会落到本机离线库里。"
                />
              </Form.Item>
              <Form.Item
                label="离线库路径"
                htmlFor="import-source-db-path"
                required
                extra={taskError
                  ? <Typography.Text type="warning">{taskError}</Typography.Text>
                  : "已按博试云桥隧定检系统的默认位置填好，一般不用改。离线库只会被只读打开，不会被复制或修改。"}
              >
                <Flex gap={8}>
                  <Input
                    id="import-source-db-path"
                    ref={sourceDbPathRef}
                    disabled={busy || loadingTasks}
                    value={sourceDbPath}
                    placeholder={loadingTasks ? "正在查找博试云桥隧定检系统的离线库…" : "离线库的完整路径"}
                    onChange={(event) => setSourceDbPath(event.target.value)}
                  />
                  <Button
                    disabled={busy}
                    loading={loadingTasks}
                    onClick={() => void loadTasks(sourceDbPath.trim() || undefined)}
                  >
                    重新读取
                  </Button>
                </Flex>
              </Form.Item>
              <Form.Item
                label="检测任务"
                htmlFor="import-source-task"
                required
                extra="一份离线库里存着你打开过的所有桥；病害与照片条数可以用来确认这一年是否已下载全。"
              >
                <Select
                  id="import-source-task"
                  ref={taskIdRef}
                  disabled={busy || loadingTasks || !tasks?.length}
                  value={taskId || undefined}
                  placeholder={loadingTasks ? "正在读取…" : tasks?.length ? "请选择" : "未读到检测任务"}
                  onChange={setTaskId}
                  options={(tasks ?? []).map((task) => ({
                    value: task.task_id,
                    label: [task.name, task.check_date ?? "未填日期",
                      `${task.defect_count} 条病害`, `${task.photo_count} 张照片`].join(" · "),
                  }))}
                />
              </Form.Item>
            </>
          ) : null}

          {retryImport || fromSourceDb ? null : (
            <>
              <Form.Item label="Word 文件" htmlFor="import-word-file" required>
                <Upload.Dragger
                  id="import-word-file"
                  accept=".docx,application/vnd.openxmlformats-officedocument.wordprocessingml.document"
                  showUploadList={false}
                  disabled={busy}
                  beforeUpload={(picked) => {
                    setFile(picked);
                    return Upload.LIST_IGNORE;
                  }}
                >
                  <Flex vertical align="center" gap={4}>
                    <InboxOutlined aria-hidden="true" />
                    {file ? (
                      <>
                        <Typography.Text strong>{file.name}</Typography.Text>
                        <Typography.Text type="secondary">{formatFileSize(file.size)} · 点击可重选</Typography.Text>
                      </>
                    ) : (
                      <>
                        <Typography.Text>点击选择，或把 .docx 拖到这里</Typography.Text>
                        <Typography.Text type="secondary">仅支持检测软件导出的 .docx</Typography.Text>
                      </>
                    )}
                    {/* 选错扩展名当场就说，不必等到点了提交才发现——拦截仍在提交处。 */}
                    {wrongExtension ? <Typography.Text type="danger">只能导入 .docx 文件。</Typography.Text> : null}
                  </Flex>
                </Upload.Dragger>
              </Form.Item>

              <Form.Item label="来源类型" htmlFor="import-source-type">
                <Select
                  id="import-source-type"
                  value={sourceType}
                  disabled={busy}
                  onChange={setSourceType}
                  options={[
                    { value: "软件导出Word", label: "软件导出Word" },
                    { value: "正式Word", label: "正式Word" },
                  ]}
                />
              </Form.Item>
            </>
          )}

          <Form.Item
            label="项目名称"
            htmlFor="import-project-name"
            required
            extra="已按桥名与年度预填，如与报告封面不一致请改成封面上的写法。"
          >
            <Input
              id="import-project-name"
              ref={projectNameRef}
              disabled={busy}
              value={projectName}
              onChange={(event) => setProjectName(event.target.value)}
            />
          </Form.Item>
        </Form>

        {phase === "uploading" ? (
          <Typography.Text type="secondary">
            {fromSourceDb ? "正在登记来源库导入…" : "正在上传并临时保存 Word…"}
          </Typography.Text>
        ) : null}
        {phase === "parsing" ? <Typography.Text type="secondary">正在解析病害、照片和评分…</Typography.Text> : null}
        {error ? <Alert type="error" showIcon title={error} /> : null}
      </Flex>
    </Modal>
  );
}
