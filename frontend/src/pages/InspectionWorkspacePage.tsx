import {
  AuditOutlined,
  DownOutlined,
  EllipsisOutlined,
  FileWordOutlined,
  ImportOutlined,
  InboxOutlined,
  LinkOutlined,
  PlusOutlined,
  ReloadOutlined,
  WarningOutlined,
} from "@ant-design/icons";
import {
  Alert,
  Button,
  Card,
  Col,
  Divider,
  Dropdown,
  Flex,
  Result,
  Row,
  Space,
  Steps,
  Table,
  Tabs,
  Tag,
  Typography,
  theme,
  type AlertProps,
  type StepsProps,
  type TableColumnsType,
} from "antd";
import dayjs from "dayjs";
import { useEffect, useMemo, useState, type ReactNode } from "react";
import { useNavigate, useParams } from "react-router-dom";

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
import {
  fetchInspectionWorkspace,
  type InspectionWorkspace,
  type WorkspaceImport,
  type WorkspaceStandardPackage,
} from "../api/workspaceApi";
import { backendBaseUrl } from "../config";
import { MetricCard, type MetricTone } from "../design-system";
import { CreateInspectionDialog } from "../workspace/CreateInspectionDialog";
import { ImportWordDialog } from "../workspace/ImportWordDialog";
import { DeleteInspectionYearDialog } from "../workspace/DeleteInspectionYearDialog";
import { DeleteImportRecordDialog } from "../workspace/DeleteImportRecordDialog";
import { useBridgeWorkspace } from "../workspace/BridgeWorkspaceShell";
import { StatusTag } from "../workspace/StatusTag";
import {
  deriveInspectionProgress,
  inspectionWorkspacePath,
  reviewPath,
  type InspectionProgress,
} from "../workspace/workspaceState";

const actionLabel = (item: WorkspaceImport) => {
  if (item.available_action === "continue_review") return "继续校对";
  if (item.available_action === "view_result") return "查看结果";
  if (item.available_action === "reupload") return "重新上传";
  if (item.available_action === "parse") return item.import_status === "解析失败" ? "重新解析" : "开始解析";
  return null;
};

const formatDateTime = (value: string | null) => (value ? dayjs(value).format("YYYY-MM-DD HH:mm") : "—");

interface ImportDialogState {
  retry: WorkspaceImport | null;
  inspectionYearId: string;
  inspectionYear: number;
}

export function InspectionWorkspacePage() {
  const { bridgeId, inspectionYearId } = useParams<{ bridgeId: string; inspectionYearId?: string }>();
  const navigate = useNavigate();
  const { user } = useAuth();
  const { token } = theme.useToken();
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
  if (!bridgeId) return <Alert type="error" showIcon title="缺少桥梁标识。" />;

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
    <Flex vertical gap={16}>
      {/* 年度改成页签横排在最上面：原来左侧一整列年份表把正文挤窄，年份通常只有几个。 */}
      {years && years.length > 0 ? (
        <Tabs
          aria-label="检测年度"
          activeKey={inspectionYearId && selectedExists ? inspectionYearId : undefined}
          onChange={(key) => navigate(inspectionWorkspacePath(bridgeId, key))}
          tabBarExtraContent={<Button icon={<PlusOutlined />} onClick={() => setShowCreate(true)}>新建年度</Button>}
          styles={{ header: { marginBottom: 0 } }}
          items={years.map((year) => ({
            key: year.id,
            label: (
              <Flex align="baseline" gap={6}>
                <span>{year.inspection_year}</span>
                <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                  {year.status}{year.version_number > 1 ? ` · V${year.version_number}` : ""}
                </Typography.Text>
              </Flex>
            ),
          }))}
        />
      ) : null}

      {error ? (
        <Card>
          <Result status="error" title="无法加载年度工作台" subTitle={error} extra={<Button onClick={refresh}>重新加载</Button>} />
        </Card>
      ) : null}
      {!error && years?.length === 0 ? (
        <Card>
          <Result
            status="info"
            title="从新建年度检测开始"
            subTitle="年度创建后，可在这里导入 Word、解析并进入全屏校对。"
            extra={<Button type="primary" onClick={() => setShowCreate(true)}>新建年度检测</Button>}
          />
        </Card>
      ) : null}
      {!error && years === null && !inspectionYearId ? <Card loading /> : null}
      {!error && !selectedExists ? <Alert type="error" showIcon title="指定年度不在当前桥梁的有效年度列表中。" /> : null}
      {!error && inspectionYearId && selectedExists && workspace === null ? <Card loading /> : null}
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
        onRefresh={refresh}
        canDelete={user?.role === "admin"}
        onDelete={() => setShowDelete(true)}
        onDeleteImport={setDeleteImport}
      /> : null}

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
    </Flex>
  );
}

const PROGRESS_TAG_COLOR: Record<string, string> = {
  completed: "success",
  review: "warning",
  parsing: "processing",
  uploaded: "warning",
  parse_failed: "error",
  empty: "default",
};

/** 规则包停用或同步异常时整行转成警示色，正常时只是一行灰字。 */
function StandardPackageText({ standard }: { standard: WorkspaceStandardPackage }) {
  const problem = !standard.is_enabled ? "已停用" : standard.sync_status !== "正常" ? standard.sync_status : null;
  return (
    <Typography.Text type={problem ? "warning" : "secondary"}>
      {standard.standard_code} · 规则包 {standard.package_version}{problem ? `（${problem}）` : ""}
    </Typography.Text>
  );
}

/**
 * 进度条上每一步写的是这一步的结果（几份资料、几条待校对、评定等级），而不是「已完成 / 待开始」，
 * 进度和数字放在一处看。
 */
function progressSteps(
  workspace: InspectionWorkspace,
  progress: InspectionProgress,
  totals: { pending: number; confirmed: number }
): Pick<StepsProps, "current" | "status" | "items"> {
  const year = workspace.inspection_year;
  const importCount = workspace.imports.length;
  const created = year.created_at ? dayjs(year.created_at).format("YYYY-MM-DD") : "已创建";
  const rating = [year.overall_grade, year.overall_score].filter((value) => value !== null && value !== "").join(" · ");

  if (progress.stage === "completed") {
    const archived = year.status === "已归档";
    return {
      // 年度确认后下一件事是出报告；系统不知道报告出没出过，只有归档才算整条流程走完。
      current: 4,
      status: archived ? "finish" : "process",
      items: [
        { title: "创建检测", content: created },
        { title: "导入资料", content: `${importCount} 份资料` },
        { title: "数据校对", content: `${totals.confirmed} 条已确认` },
        { title: "系统评定", content: rating || "已完成" },
        { title: "生成报告", content: archived ? "已归档" : "可生成" },
      ],
    };
  }
  if (progress.stage === "review") {
    return {
      current: 2,
      status: "process",
      items: [
        { title: "创建检测", content: created },
        { title: "导入资料", content: `${importCount} 份资料` },
        { title: "数据校对", content: totals.pending > 0 ? `${totals.pending} 条待校对` : "校对中" },
        { title: "系统评定", content: "待开始" },
        { title: "生成报告", content: "待开始" },
      ],
    };
  }
  const importContent: Record<string, string> = {
    empty: "待导入",
    uploaded: "待解析",
    parsing: "解析中",
    parse_failed: "解析失败",
  };
  return {
    current: 1,
    status: progress.stage === "parse_failed" ? "error" : "process",
    items: [
      { title: "创建检测", content: created },
      { title: "导入资料", content: importContent[progress.stage] ?? progress.label },
      { title: "数据校对", content: "待开始" },
      { title: "系统评定", content: "待开始" },
      { title: "生成报告", content: "待开始" },
    ],
  };
}

function AnnualWorkspace({ workspace, bridgeId, onImport, onRetry, onRefresh, canDelete, onDelete, onDeleteImport }: {
  workspace: InspectionWorkspace;
  bridgeId: string;
  onImport: () => void;
  onRetry: (item: WorkspaceImport) => void;
  onRefresh: () => void;
  canDelete: boolean;
  onDelete: () => void;
  onDeleteImport: (item: WorkspaceImport) => void;
}) {
  const navigate = useNavigate();
  const { token } = theme.useToken();
  const year = workspace.inspection_year;
  const imports = workspace.imports;
  const progress = deriveInspectionProgress(year, imports);
  const totals = imports.reduce(
    (sum, item) => ({
      pending: sum.pending + item.statistics.pending_count,
      confirmed: sum.confirmed + item.statistics.confirmed_count,
    }),
    { pending: 0, confirmed: 0 }
  );
  const errorCount = imports.filter((item) => item.import_status === "解析失败").length;
  const yearPath = inspectionWorkspacePath(bridgeId, year.id);
  const profile = workspace.standard_profile;
  const openReview = (item: WorkspaceImport) => navigate(reviewPath(bridgeId, year.id, item.id));

  const runAction = (item: WorkspaceImport) => {
    if (item.available_action === "parse") onRetry(item);
    else if (item.available_action === "reupload") onImport();
    else openReview(item);
  };

  // 为 0 的指标转灰，只让真正需要处理的数字带颜色。
  const metrics: Array<{ label: string; value: number; icon: JSX.Element; tone: MetricTone }> = [
    { label: "导入记录", value: imports.length, icon: <ImportOutlined />, tone: "primary" },
    { label: "待绑定项", value: workspace.pending.unbound_observation_count, icon: <LinkOutlined />, tone: "warning" },
    { label: "待校对项", value: totals.pending, icon: <AuditOutlined />, tone: "warning" },
    { label: "异常提醒", value: errorCount, icon: <WarningOutlined />, tone: "error" },
  ];

  const nextStep = nextStepHint(progress, imports, totals.pending);

  // 每一列都给宽度：原来「资料」列不设宽度，宽屏上多出来的几百像素全堆在资料名后面。
  // 列宽都定了以后，多出来的宽度按比例分给各列，状态标签也并到资料名后面，少一列空白。
  const columns: TableColumnsType<WorkspaceImport> = [
    {
      title: "资料",
      key: "import",
      width: 260,
      render: (_, item) => (
        <Flex vertical gap={2}>
          <Flex align="center" gap={8} wrap>
            <Typography.Text strong>{item.import_name}</Typography.Text>
            <StatusTag status={item.import_status} />
          </Flex>
          <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
            {item.system_number} · {item.source_type}
          </Typography.Text>
          {item.import_status === "解析失败" && item.error_message ? <Typography.Text type="danger">解析失败：{item.error_message}</Typography.Text> : null}
          {item.import_status === "解析失败" && item.temporary_source_expires_at ? (
            <Typography.Text type="secondary">临时 Word 保留至 {formatDateTime(item.temporary_source_expires_at)}</Typography.Text>
          ) : null}
          {item.available_action === "reupload" ? <Typography.Text type="danger">原临时 Word 已不可用，请重新上传。</Typography.Text> : null}
          {item.edit_lock ? <Typography.Text type="warning">{item.edit_lock.owner_display_name} 正在编辑</Typography.Text> : null}
        </Flex>
      ),
    },
    { title: "病害", key: "defects", width: 88, align: "center", render: (_, item) => item.statistics.defect_count.toLocaleString() },
    { title: "照片", key: "photos", width: 88, align: "center", render: (_, item) => item.statistics.photo_count.toLocaleString() },
    {
      title: "待校对",
      key: "pending",
      width: 88,
      align: "center",
      render: (_, item) => (
        <Typography.Text type={item.statistics.pending_count > 0 ? "warning" : "secondary"} strong={item.statistics.pending_count > 0}>
          {item.statistics.pending_count.toLocaleString()}
        </Typography.Text>
      ),
    },
    { title: "已确认", key: "confirmed", width: 88, align: "center", render: (_, item) => item.statistics.confirmed_count.toLocaleString() },
    {
      title: "导入时间 / 导入人",
      key: "imported",
      // 数字右对齐时会和这一列左对齐的时间贴在一起：四个统计列改成居中，这一列左侧再多留一段空。
      width: 180 + token.paddingXL,
      onHeaderCell: () => ({ style: { paddingInlineStart: token.paddingXL } }),
      onCell: () => ({ style: { paddingInlineStart: token.paddingXL } }),
      render: (_, item) => (
        <Flex vertical gap={2}>
          <Typography.Text style={{ whiteSpace: "nowrap" }}>{formatDateTime(item.created_at)}</Typography.Text>
          <Typography.Text type="secondary" ellipsis style={{ fontSize: token.fontSizeSM, maxWidth: 150 }}>
            {item.importer_name ?? "—"}
          </Typography.Text>
        </Flex>
      ),
    },
    {
      title: "操作",
      key: "actions",
      width: 168,
      render: (_, item) => {
        const label = actionLabel(item);
        return (
          <Space size={4} style={{ whiteSpace: "nowrap" }}>
            {label ? (
              <Button type="link" size="small" onClick={() => runAction(item)}>{label}</Button>
            ) : (
              <Typography.Text type="secondary">处理中</Typography.Text>
            )}
            {/* 删除是破坏性操作，收进「更多」；一页可能有多条导入记录，按钮名带上记录名才区分得开。 */}
            {canDelete ? (
              <>
                <Divider vertical />
                <Dropdown
                  trigger={["click"]}
                  menu={{ items: [{ key: "delete", label: "删除导入记录", danger: true, onClick: () => onDeleteImport(item) }] }}
                >
                  <Button type="link" size="small" icon={<DownOutlined />} iconPlacement="end" aria-label={`更多操作 ${item.import_name}`}>
                    更多
                  </Button>
                </Dropdown>
              </>
            ) : null}
          </Space>
        );
      },
    },
  ];

  return (
    <>
      <Card>
        <Flex vertical gap={20}>
          <Flex align="flex-start" justify="space-between" gap={16} wrap>
            <Flex vertical gap={6} style={{ minWidth: 0 }}>
              <Flex align="center" gap={10} wrap>
                <Typography.Title level={3} style={{ margin: 0 }}>{year.inspection_year} 年度检测</Typography.Title>
                <Tag color={PROGRESS_TAG_COLOR[progress.stage] ?? "default"}>{progress.label}</Tag>
              </Flex>
              <Space wrap size={[0, 4]} separator={<Divider vertical />}>
                <Typography.Text type="secondary">{year.system_number}</Typography.Text>
                <Typography.Text type="secondary">当前版本 V{year.version_number}</Typography.Text>
                {profile ? <StandardPackageText standard={profile.technical_condition} /> : null}
                {profile ? <StandardPackageText standard={profile.maintenance} /> : null}
                {profile ? null : <Typography.Text type="warning"><WarningOutlined /> 历史年度未绑定规范组合</Typography.Text>}
              </Space>
            </Flex>
            {/* 年度确认后「生成报告」是主操作；之前的主操作在下一步提示里。 */}
            <Space wrap>
              <Button
                type={progress.stage === "completed" ? "primary" : "default"}
                icon={<FileWordOutlined />}
                onClick={() => navigate(`${yearPath}/report`)}
              >
                生成报告
              </Button>
              {year.is_current ? <Button icon={<ImportOutlined />} onClick={onImport}>导入资料</Button> : null}
              {canDelete ? (
                <Dropdown
                  trigger={["click"]}
                  menu={{ items: [{ key: "delete", label: "删除年度", danger: true, onClick: onDelete }] }}
                >
                  <Button icon={<EllipsisOutlined />} aria-label="更多年度操作" />
                </Dropdown>
              ) : null}
            </Space>
          </Flex>

          <Steps size="small" {...progressSteps(workspace, progress, totals)} />

          {nextStep ? (
            <Alert
              type={nextStep.type}
              showIcon
              role="status"
              title={nextStep.title}
              action={nextStep.target && nextStep.actionLabel ? (
                <Button size="small" type="primary" onClick={() => runAction(nextStep.target!)}>{nextStep.actionLabel}</Button>
              ) : nextStep.refresh ? (
                <Button size="small" icon={<ReloadOutlined />} onClick={onRefresh}>刷新</Button>
              ) : null}
            />
          ) : null}
        </Flex>
      </Card>

      <Row gutter={[16, 16]} role="group" aria-label="年度检测概况">
        {metrics.map((item) => (
          <Col key={item.label} xs={12} xl={6}>
            <MetricCard size="small" title={item.label} value={item.value} icon={item.icon} tone={item.value > 0 ? item.tone : "neutral"} />
          </Col>
        ))}
      </Row>

      <Card
        title={
          <Space size={8}>
            <span>导入记录</span>
            {imports.length > 0 ? <Typography.Text type="secondary" style={{ fontWeight: "normal" }}>{imports.length} 条</Typography.Text> : null}
          </Space>
        }
        extra={imports.length > 0 ? <Button icon={<ReloadOutlined />} onClick={onRefresh}>刷新</Button> : null}
      >
        {imports.length === 0 ? (
          <Flex vertical align="center" gap={12}>
            {/* 图标不再用 Word：来源已经有三种，点名其中一种会误导。 */}
            <InboxOutlined style={{ fontSize: 44, color: token.colorPrimary }} aria-hidden="true" />
            <Typography.Title level={4} style={{ margin: 0 }}>尚未导入检测资料</Typography.Title>
            <Typography.Text type="secondary">导入后可进行构件绑定与数据校对。</Typography.Text>
            {/* 三种来源摊开成一行三项：哪个能用、哪个还没做，一眼看完。 */}
            <Row gutter={[12, 12]} style={{ width: "100%", maxWidth: 880 }}>
              <SourceOption title="博试云桥隧定检系统" description="读取本机离线库" />
              <SourceOption title="Word 检测资料" description="软件导出 Word 或正式报告" />
              <SourceOption title="移动端现场采集" description="开发中" disabled />
            </Row>
            <Button type="primary" onClick={onImport}>导入检测资料</Button>
          </Flex>
        ) : (
          <Table<WorkspaceImport>
            rowKey="id"
            size="middle"
            columns={columns}
            dataSource={imports}
            pagination={false}
            scroll={{ x: 960 }}
          />
        )}
      </Card>
    </>
  );
}

function SourceOption({ title, description, disabled }: { title: ReactNode; description: ReactNode; disabled?: boolean }) {
  return (
    <Col xs={24} md={8}>
      <Card size="small">
        <Flex vertical>
          <Typography.Text strong disabled={disabled}>{title}</Typography.Text>
          <Typography.Text type="secondary">{description}</Typography.Text>
        </Flex>
      </Card>
    </Col>
  );
}

/**
 * 进度条下面那一行「下一步」：写明卡在哪条资料上、该点哪个按钮。
 * 还没导入资料时下面的空状态本身就是入口，年度确认后主操作是「生成报告」，这两种不再提示。
 */
function nextStepHint(
  progress: InspectionProgress,
  imports: WorkspaceImport[],
  pending: number
): { type: NonNullable<AlertProps["type"]>; title: string; target?: WorkspaceImport; actionLabel?: string | null; refresh?: boolean } | null {
  if (progress.stage === "review") {
    const target = imports.find((item) => item.available_action === "continue_review");
    return {
      type: "info",
      title: `下一步：数据校对${pending > 0 ? `，还有 ${pending} 条待校对` : ""}。`,
      target,
      actionLabel: target ? actionLabel(target) : null,
    };
  }
  if (progress.stage === "uploaded") {
    const target = imports.find((item) => item.import_status === "已上传");
    return {
      type: "info",
      title: `下一步：解析已上传的资料「${target?.import_name ?? ""}」。`,
      target,
      actionLabel: target ? actionLabel(target) : null,
    };
  }
  if (progress.stage === "parsing") {
    const target = imports.find((item) => item.import_status === "解析中");
    return { type: "info", title: `资料「${target?.import_name ?? ""}」正在解析，完成后即可开始校对。`, refresh: true };
  }
  if (progress.stage === "parse_failed") {
    const target = imports.find((item) => item.import_status === "解析失败");
    return {
      type: "warning",
      title: `资料「${target?.import_name ?? ""}」解析失败，原因见下方导入记录。`,
      target,
      actionLabel: target ? actionLabel(target) : null,
    };
  }
  return null;
}
