import {
  CheckCircleOutlined,
  CloudUploadOutlined,
  DownloadOutlined,
  PlusOutlined,
  ReloadOutlined,
  SwapOutlined,
} from "@ant-design/icons";
import {
  Alert,
  Button,
  Card,
  Drawer,
  Empty,
  Input,
  Modal,
  Popconfirm,
  Select,
  Space,
  Table,
  Tag,
  Upload,
  type TableProps,
  type UploadFile,
} from "antd";
import { useCallback, useEffect, useMemo, useState } from "react";

import {
  deleteReportTemplate,
  fetchReportTemplates,
  replaceReportTemplateFile,
  downloadReportTemplateFile,
  setReportTemplateDefault,
  setReportTemplateEnabled,
  uploadReportTemplate,
  structurePartLabel,
  type ReportTemplate,
  type TemplateIssue,
} from "../api/reportApi";
import { useAuth } from "../auth/AuthContext";
import { reportErrorMessage, templateValidationIssues } from "../report/reportErrors";
import {
  DEFAULT_REQUIRED_ROLES,
  PERIODIC_INSPECTION_V1_NUMBER_FORMATS,
  PERSONNEL_ROLE_OPTIONS,
  formatMapToRows,
  rowsToFormatMap,
  type NumberFormatRow,
} from "../report/reportNumberFormats";
import "./ReportAdminPages.css";

/**
 * 系统管理 · 报告模板（设计 §21.1）。
 *
 * 模板是报告的骨架：章节顺序、版式和编号规则都在里面，生成器只负责往锚点里填内容。
 * 所以这一页的每个操作都要能解释清楚后果——尤其是"校验不通过"，必须把明细摆出来，
 * 只说一句"模板不合契约"管理员无从下手。
 */
export function ReportTemplatesPage() {
  const { user } = useAuth();
  const isAdmin = user?.role === "admin";
  const [templates, setTemplates] = useState<ReportTemplate[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busyId, setBusyId] = useState<string | null>(null);
  const [uploadOpen, setUploadOpen] = useState(false);
  const [replacing, setReplacing] = useState<ReportTemplate | null>(null);
  const [inspecting, setInspecting] = useState<ReportTemplate | null>(null);

  const load = useCallback(async () => {
    setError(null);
    try {
      setTemplates(await fetchReportTemplates());
    } catch (caught) {
      setError(reportErrorMessage(caught));
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  /** 下载不改任何东西，所以不走 run——它做完会重新拉一遍列表，没有意义。 */
  async function download(item: ReportTemplate) {
    setError(null);
    try {
      await downloadReportTemplateFile(item.id, item.file_name);
    } catch (caught) {
      setError(reportErrorMessage(caught));
    }
  }

  async function run(id: string, action: () => Promise<unknown>) {
    setBusyId(id);
    setError(null);
    try {
      await action();
      await load();
    } catch (caught) {
      setError(reportErrorMessage(caught));
    } finally {
      setBusyId(null);
    }
  }

  const columns: TableProps<ReportTemplate>["columns"] = useMemo(
    () => [
      {
        title: "模板",
        dataIndex: "template_name",
        key: "template_name",
        width: 260,
        render: (_value, item) => (
          <div className="report-template-name">
            <strong>{item.template_name}</strong>
            <span>{item.template_code}</span>
          </div>
        ),
      },
      {
        title: "契约",
        dataIndex: "contract_type",
        key: "contract_type",
        width: 180,
        ellipsis: true,
      },
      {
        title: "校验",
        dataIndex: "validation_status",
        key: "validation_status",
        width: 110,
        render: (value: string) =>
          value === "valid" ? (
            <Tag color="green">通过</Tag>
          ) : (
            <Tag color="red">{value === "invalid" ? "未通过" : value}</Tag>
          ),
      },
      {
        title: "状态",
        key: "state",
        width: 150,
        render: (_value, item) => (
          <Space size={4}>
            <Tag color={item.is_enabled ? "blue" : "default"}>
              {item.is_enabled ? "已启用" : "已停用"}
            </Tag>
            {item.is_default ? <Tag color="gold">默认</Tag> : null}
          </Space>
        ),
      },
      {
        title: "被引用",
        dataIndex: "usage_count",
        key: "usage_count",
        width: 90,
        align: "right",
        render: (value: number) => `${value} 个年度`,
      },
      {
        title: "更新",
        key: "updated",
        width: 200,
        render: (_value, item) => (
          <div className="report-template-updated">
            <span>{item.updated_at.slice(0, 19)}</span>
            <span>{item.updated_by_display_name ?? "—"}</span>
          </div>
        ),
      },
      {
        title: "操作",
        key: "actions",
        width: 280,
        fixed: "right",
        render: (_value, item) => (
          <Space size={2} wrap={false} className="report-table-actions">
            <Button type="link" size="small" onClick={() => setInspecting(item)}>
              锚点
            </Button>
            {isAdmin ? (
              <>
                <Button
                  type="link"
                  size="small"
                  icon={<DownloadOutlined />}
                  onClick={() => void download(item)}
                >
                  源文件
                </Button>
                <Button
                  type="link"
                  size="small"
                  icon={<SwapOutlined />}
                  onClick={() => setReplacing(item)}
                >
                  替换
                </Button>
                {item.is_default ? null : (
                  <Button
                    type="link"
                    size="small"
                    disabled={!item.is_enabled || busyId !== null}
                    onClick={() => void run(item.id, () => setReportTemplateDefault(item.id))}
                  >
                    设为默认
                  </Button>
                )}
                <Button
                  type="link"
                  size="small"
                  disabled={busyId !== null || (item.is_enabled && !item.can_disable)}
                  onClick={() =>
                    void run(item.id, () => setReportTemplateEnabled(item.id, !item.is_enabled))
                  }
                >
                  {item.is_enabled ? "停用" : "启用"}
                </Button>
                <Popconfirm
                  title="删除这份模板？"
                  description="模板文件会一并清理，无法恢复。"
                  okText="删除"
                  cancelText="取消"
                  disabled={!item.can_delete}
                  onConfirm={() => void run(item.id, () => deleteReportTemplate(item.id))}
                >
                  <Button type="link" size="small" danger disabled={!item.can_delete}>
                    删除
                  </Button>
                </Popconfirm>
              </>
            ) : null}
          </Space>
        ),
      },
    ],
    [busyId, isAdmin],
  );

  return (
    <section className="report-admin-page">
      <header className="workspace-page-header">
        <div>
          <h1>报告模板</h1>
          <p>
            模板定下报告的章节顺序、版式和编号规则；生成器只往锚点里填内容，不写死章节号。
          </p>
        </div>
        {isAdmin ? (
          <div className="workspace-page-actions">
            <Button icon={<ReloadOutlined />} onClick={() => void load()}>
              刷新
            </Button>
            <Button type="primary" icon={<PlusOutlined />} onClick={() => setUploadOpen(true)}>
              上传模板
            </Button>
          </div>
        ) : null}
      </header>

      {error ? (
        <Alert type="error" showIcon title={error} closable onClose={() => setError(null)} />
      ) : null}

      <Card className="report-admin-card" title={`模板列表（${templates?.length ?? 0}）`}>
        <Table<ReportTemplate>
          rowKey="id"
          loading={templates === null && error === null}
          columns={columns}
          dataSource={templates ?? []}
          pagination={false}
          scroll={{ x: 1280 }}
          locale={{
            emptyText: (
              <Empty
                image={Empty.PRESENTED_IMAGE_SIMPLE}
                description="还没有模板。上传一份通过契约校验的 .docx 即可启用。"
              />
            ),
          }}
        />
      </Card>

      <UploadTemplateModal
        open={uploadOpen}
        onClose={() => setUploadOpen(false)}
        onUploaded={() => {
          setUploadOpen(false);
          void load();
        }}
      />
      <ReplaceTemplateModal
        template={replacing}
        onClose={() => setReplacing(null)}
        onReplaced={() => {
          setReplacing(null);
          void load();
        }}
      />
      <TemplateDetailDrawer template={inspecting} onClose={() => setInspecting(null)} />
    </section>
  );
}

/** 校验明细。每一条都指出模板的哪里要改，位置串直接给出来。 */
function ValidationIssueList({ issues }: { issues: TemplateIssue[] }) {
  if (issues.length === 0) return null;
  return (
    <ul className="report-issue-list">
      {issues.map((issue, index) => (
        <li key={`${issue.code}-${index}`} className={`is-${issue.severity}`}>
          <span className="report-issue-code">{issue.code}</span>
          <span>{issue.message}</span>
          {issue.location ? <em>{issue.location}</em> : null}
        </li>
      ))}
    </ul>
  );
}

function UploadTemplateModal({
  open,
  onClose,
  onUploaded,
}: {
  open: boolean;
  onClose: () => void;
  onUploaded: () => void;
}) {
  const [code, setCode] = useState("");
  const [name, setName] = useState("");
  const [description, setDescription] = useState("");
  const [roles, setRoles] = useState<string[]>(DEFAULT_REQUIRED_ROLES);
  const [formats, setFormats] = useState<NumberFormatRow[]>(
    PERIODIC_INSPECTION_V1_NUMBER_FORMATS,
  );
  const [file, setFile] = useState<UploadFile | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [issues, setIssues] = useState<TemplateIssue[]>([]);

  useEffect(() => {
    if (!open) return;
    setCode("");
    setName("");
    setDescription("");
    setRoles(DEFAULT_REQUIRED_ROLES);
    setFormats(PERIODIC_INSPECTION_V1_NUMBER_FORMATS);
    setFile(null);
    setError(null);
    setIssues([]);
  }, [open]);

  async function submit() {
    const raw = file?.originFileObj as File | undefined;
    if (!raw) {
      setError("请选择一份 .docx 模板文件。");
      return;
    }
    setBusy(true);
    setError(null);
    setIssues([]);
    try {
      await uploadReportTemplate(raw, {
        template_code: code.trim(),
        template_name: name.trim(),
        description: description.trim() || undefined,
        contract_config: {
          table_number_formats: rowsToFormatMap(formats),
          required_personnel_roles: roles,
        },
      });
      onUploaded();
    } catch (caught) {
      setError(reportErrorMessage(caught));
      setIssues(templateValidationIssues(caught));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      open={open}
      centered
      width={760}
      title="上传报告模板"
      okText="上传并校验"
      cancelText="取消"
      confirmLoading={busy}
      onOk={() => void submit()}
      onCancel={onClose}
      okButtonProps={{ disabled: !code.trim() || !name.trim() || !file }}
    >
      <div className="report-form-grid">
        <label>
          <span>模板代码</span>
          <Input
            value={code}
            onChange={(event) => setCode(event.target.value)}
            placeholder="PERIODIC_INSPECTION_V2"
          />
        </label>
        <label>
          <span>模板名称</span>
          <Input
            value={name}
            onChange={(event) => setName(event.target.value)}
            placeholder="定期检测报告标准模板"
          />
        </label>
        <label className="is-full">
          <span>说明</span>
          <Input
            value={description}
            onChange={(event) => setDescription(event.target.value)}
            placeholder="选填"
          />
        </label>
        <label className="is-full">
          <span>所需人员角色</span>
          <Select
            mode="multiple"
            value={roles}
            onChange={setRoles}
            options={PERSONNEL_ROLE_OPTIONS}
            placeholder="签字页按这些角色决定必填项"
          />
        </label>
      </div>

      <h4 className="report-form-subtitle">表号与图号格式</h4>
      <p className="report-form-hint">
        {"{n}"} 是序号占位。配同一个格式串的内容块共用一条序列，按文档顺序递增——
        4.1.1 与 4.1.2 都配「表4.1-{"{n}"}」就得到 表4.1-1 和 表4.1-2。
      </p>
      <div className="report-format-rows">
        {formats.map((row, index) => (
          <div className="report-format-row" key={row.key}>
            <span title={row.key}>{row.label}</span>
            <Input
              value={row.format}
              aria-label={`${row.label}的编号格式`}
              onChange={(event) => {
                const next = [...formats];
                next[index] = { ...row, format: event.target.value };
                setFormats(next);
              }}
            />
          </div>
        ))}
      </div>

      <h4 className="report-form-subtitle">模板文件</h4>
      <Upload.Dragger
        accept=".docx"
        maxCount={1}
        beforeUpload={() => false}
        fileList={file ? [file] : []}
        onChange={(info) => setFile(info.fileList[0] ?? null)}
      >
        <p className="ant-upload-drag-icon">
          <CloudUploadOutlined />
        </p>
        <p className="ant-upload-text">把 .docx 模板拖到这里，或点击选择</p>
        <p className="ant-upload-hint">校验不通过时不写库，也不会动已有模板。</p>
      </Upload.Dragger>

      {error ? <Alert className="report-form-alert" type="error" showIcon title={error} /> : null}
      <ValidationIssueList issues={issues} />
    </Modal>
  );
}

function ReplaceTemplateModal({
  template,
  onClose,
  onReplaced,
}: {
  template: ReportTemplate | null;
  onClose: () => void;
  onReplaced: () => void;
}) {
  const [file, setFile] = useState<UploadFile | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [issues, setIssues] = useState<TemplateIssue[]>([]);

  useEffect(() => {
    setFile(null);
    setError(null);
    setIssues([]);
  }, [template]);

  async function submit() {
    const raw = file?.originFileObj as File | undefined;
    if (!template || !raw) return;
    setBusy(true);
    setError(null);
    setIssues([]);
    try {
      await replaceReportTemplateFile(template.id, raw);
      onReplaced();
    } catch (caught) {
      setError(reportErrorMessage(caught));
      setIssues(templateValidationIssues(caught));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      open={template !== null}
      centered
      width={640}
      title={template ? `替换「${template.template_name}」的文件` : "替换模板文件"}
      okText="替换并校验"
      cancelText="取消"
      confirmLoading={busy}
      okButtonProps={{ disabled: !file }}
      onOk={() => void submit()}
      onCancel={onClose}
    >
      <Alert
        type="info"
        showIcon
        title="校验不通过时保留旧模板，一个字节都不动。"
        description="编号格式和所需角色沿用现有配置，这里只换文件。"
      />
      <Upload.Dragger
        className="report-form-upload"
        accept=".docx"
        maxCount={1}
        beforeUpload={() => false}
        fileList={file ? [file] : []}
        onChange={(info) => setFile(info.fileList[0] ?? null)}
      >
        <p className="ant-upload-drag-icon">
          <CloudUploadOutlined />
        </p>
        <p className="ant-upload-text">把新的 .docx 拖到这里，或点击选择</p>
      </Upload.Dragger>
      {error ? <Alert className="report-form-alert" type="error" showIcon title={error} /> : null}
      <ValidationIssueList issues={issues} />
    </Modal>
  );
}

/** 锚点与配置详情：模板到底把哪些内容块排在了哪儿（设计 §21.1）。 */
function TemplateDetailDrawer({
  template,
  onClose,
}: {
  template: ReportTemplate | null;
  onClose: () => void;
}) {
  const result = template?.validation_result as
    | { anchors_in_document_order?: string[]; issues?: TemplateIssue[]; fields_used?: string[] }
    | undefined;
  const anchors = result?.anchors_in_document_order ?? [];
  const formats = formatMapToRows(template?.contract_config?.table_number_formats);
  const roles = template?.contract_config?.required_personnel_roles ?? [];

  return (
    <Drawer
      open={template !== null}
      onClose={onClose}
      size={560}
      title={template ? `${template.template_name} · 锚点与配置` : "模板详情"}
    >
      {template === null ? null : (
        <div className="report-drawer-body">
          <section>
            <h4>内容锚点（文档顺序）</h4>
            {anchors.length === 0 ? (
              <p className="report-muted">这份模板还没有校验结果，重新上传后即可看到。</p>
            ) : (
              <ol className="report-anchor-list">
                {anchors.map((anchor, index) => {
                  const [block, part] = anchor.split(":");
                  return (
                    <li key={`${anchor}-${index}`}>
                      <code>{block}</code>
                      {part ? <Tag>{structurePartLabel(part)}</Tag> : null}
                    </li>
                  );
                })}
              </ol>
            )}
          </section>

          <section>
            <h4>表号与图号格式</h4>
            {formats.length === 0 ? (
              <p className="report-muted">未配置。</p>
            ) : (
              <ul className="report-kv-list">
                {formats.map((row) => (
                  <li key={row.key}>
                    <span>{row.label}</span>
                    <code>{row.format}</code>
                  </li>
                ))}
              </ul>
            )}
          </section>

          <section>
            <h4>所需人员角色</h4>
            {roles.length === 0 ? (
              <p className="report-muted">未配置。</p>
            ) : (
              <Space size={4} wrap>
                {roles.map((role) => (
                  <Tag key={role} icon={<CheckCircleOutlined />}>
                    {PERSONNEL_ROLE_OPTIONS.find((option) => option.value === role)?.label ?? role}
                  </Tag>
                ))}
              </Space>
            )}
          </section>

          <section>
            <h4>校验明细</h4>
            {(result?.issues ?? []).length === 0 ? (
              <p className="report-muted">没有问题。</p>
            ) : (
              <ValidationIssueList issues={result?.issues ?? []} />
            )}
          </section>
        </div>
      )}
    </Drawer>
  );
}
