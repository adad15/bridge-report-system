import {
  AuditOutlined,
  CheckCircleOutlined,
  DatabaseOutlined,
  PlusOutlined,
  WarningOutlined,
} from "@ant-design/icons";
import {
  Alert,
  Button,
  Card,
  Col,
  Divider,
  Empty,
  Flex,
  Form,
  Modal,
  Progress,
  Row,
  Select,
  Skeleton,
  Space,
  Table,
  Tag,
  Typography,
  theme,
  type TableColumnsType,
} from "antd";
import { useCallback, useEffect, useMemo, useState, type ReactNode } from "react";
import { useNavigate } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import { fetchBridges, type BridgeSummary } from "../api/navigationApi";
import { useAuth } from "../auth/AuthContext";
import { backendBaseUrl } from "../config";
import { MetricCard, PageHeader, type MetricTone } from "../design-system";
import { CreateInspectionDialog } from "../workspace/CreateInspectionDialog";
import { bridgeOverviewPath, inspectionWorkspacePath } from "../workspace/workspaceState";

const kScaleOrder = ["特大桥", "大桥", "中桥", "小桥"];
const kPendingPageSize = 6;
const kAttentionLimit = 5;

/** 评定等级在库里是「2类」这样的文字；只取 1–5 的数字，认不出来的按未评定处理。 */
function gradeLevel(grade: string | null): number | null {
  const matched = grade?.match(/[1-5]/);
  return matched ? Number(matched[0]) : null;
}

const kGradeTagColor: Record<number, string> = { 1: "success", 2: "processing", 3: "warning", 4: "volcano", 5: "error" };

function GradeTag({ level }: { level: number | null }) {
  return level === null
    ? <Tag variant="filled">未评定</Tag>
    : <Tag color={kGradeTagColor[level]} variant="filled">{level}类</Tag>;
}

const formatScore = (score: number | null) => (score === null ? null : `${score.toFixed(2)} 分`);

interface DashboardMetric {
  title: string;
  value: number;
  suffix: string;
  description: string;
  tone: MetricTone;
  icon: ReactNode;
}

export function WorkbenchPage() {
  const navigate = useNavigate();
  const { user } = useAuth();
  const { token } = theme.useToken();
  const [bridges, setBridges] = useState<BridgeSummary[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [pickingBridge, setPickingBridge] = useState(false);
  const [pickedBridgeId, setPickedBridgeId] = useState<string | null>(null);
  const [creatingFor, setCreatingFor] = useState<string | null>(null);

  const load = useCallback(async () => {
    try {
      setBridges(await fetchBridges(backendBaseUrl));
      setError(null);
    } catch (caught) {
      setError(caught instanceof ApiError ? caught.message : "工作台数据加载失败");
      setBridges([]);
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  // 工作台只用桥梁列表一个接口：待处理数、最近一次已确认评定都在里面，不再摆没有数据来源的卡片。
  const dashboard = useMemo(() => {
    const source = bridges ?? [];
    const currentYear = new Date().getFullYear();
    const pending = source
      .filter((bridge) => bridge.pending_count > 0)
      .sort((left, right) => right.pending_count - left.pending_count || left.system_number.localeCompare(right.system_number));
    const withLevel = source.map((bridge) => ({ bridge, level: gradeLevel(bridge.latest_overall_grade) }));
    const attention = withLevel
      .filter((item) => item.level !== null && item.level >= 3)
      .sort((left, right) => (left.bridge.latest_overall_score ?? Infinity) - (right.bridge.latest_overall_score ?? Infinity));
    const scales = new Map<string, number>();
    for (const bridge of source) {
      const scale = bridge.bridge_scale ?? "规模未填";
      scales.set(scale, (scales.get(scale) ?? 0) + 1);
    }
    const scaleNote = [...scales.entries()]
      .sort(([left], [right]) => rank(left) - rank(right))
      .map(([scale, count]) => `${scale} ${count}`)
      .join(" · ");
    return {
      currentYear,
      pending,
      pendingTotal: pending.reduce((sum, bridge) => sum + bridge.pending_count, 0),
      assessedThisYear: source.filter((bridge) => bridge.latest_inspection_year === currentYear).length,
      attention,
      severeCount: attention.filter((item) => (item.level ?? 0) >= 4).length,
      grades: [1, 2, 3, 4, 5, null].map((level) => ({
        level,
        count: withLevel.filter((item) => item.level === level).length,
      })),
      scaleNote,
      total: source.length,
    };
  }, [bridges]);

  const metrics: DashboardMetric[] = [
    {
      title: "在册桥梁",
      value: dashboard.total,
      suffix: "座",
      description: dashboard.scaleNote || "尚未建立桥梁档案",
      tone: "primary",
      icon: <DatabaseOutlined />,
    },
    {
      title: "待处理事项",
      value: dashboard.pendingTotal,
      suffix: "项",
      description: dashboard.pending.length > 0 ? `涉及 ${dashboard.pending.length} 座桥梁` : "没有待处理的资料或病害",
      tone: "warning",
      icon: <AuditOutlined />,
    },
    {
      title: `${dashboard.currentYear} 年已完成评定`,
      value: dashboard.assessedThisYear,
      suffix: "座",
      description: `还有 ${dashboard.total - dashboard.assessedThisYear} 座未完成本年度评定`,
      tone: "success",
      icon: <CheckCircleOutlined />,
    },
    {
      title: "3 类及以下桥梁",
      value: dashboard.attention.length,
      suffix: "座",
      description: `其中 4、5 类 ${dashboard.severeCount} 座`,
      tone: "error",
      icon: <WarningOutlined />,
    },
  ];

  const hour = new Date().getHours();
  const dayPeriod = hour < 12 ? "上午好" : hour < 18 ? "下午好" : "晚上好";
  const greeting = bridges === null
    ? `${dayPeriod}，${user?.display_name ?? ""}。`
    : `${dayPeriod}，${user?.display_name ?? ""}。${dashboard.pending.length > 0 ? `当前有 ${dashboard.pending.length} 座桥有待处理事项。` : "当前没有待处理事项。"}`;

  const pendingColumns: TableColumnsType<BridgeSummary> = [
    {
      title: "桥梁",
      key: "bridge",
      render: (_, bridge) => (
        <Flex vertical gap={2}>
          <Typography.Text strong>{bridge.bridge_name}</Typography.Text>
          <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
            {bridge.system_number} · {bridge.route_name ?? "路线未填写"}
          </Typography.Text>
        </Flex>
      ),
    },
    { title: "规模", key: "scale", width: 96, render: (_, bridge) => bridge.bridge_scale ?? "—" },
    {
      title: "待处理",
      key: "pending",
      width: 110,
      render: (_, bridge) => (
        <Space size={6}>
          <Typography.Text type="warning" strong>{bridge.pending_count.toLocaleString()}</Typography.Text>
          <Typography.Text type="secondary">项</Typography.Text>
        </Space>
      ),
    },
    {
      title: "最近评定",
      key: "latest",
      width: 240,
      render: (_, bridge) => (
        <Space size={8}>
          <GradeTag level={gradeLevel(bridge.latest_overall_grade)} />
          {bridge.latest_inspection_year === null ? (
            <Typography.Text type="secondary">尚无已确认年度</Typography.Text>
          ) : (
            <Typography.Text>
              {[`${bridge.latest_inspection_year} 年`, formatScore(bridge.latest_overall_score)].filter(Boolean).join(" · ")}
            </Typography.Text>
          )}
        </Space>
      ),
    },
    {
      title: "操作",
      key: "actions",
      width: 100,
      // 待处理里既有导入资料也有待整理的病害，列表接口分不出是哪一种，先进桥梁概览再往下走。
      render: (_, bridge) => (
        <Button type="link" size="small" aria-label={`去处理 ${bridge.bridge_name}`} onClick={() => navigate(bridgeOverviewPath(bridge.id))}>
          去处理
        </Button>
      ),
    },
  ];

  const maxGradeCount = Math.max(1, ...dashboard.grades.map((item) => item.count));
  const gradeColor = (level: number | null) => (level === null ? token.colorTextQuaternary : {
    1: token.colorSuccess,
    2: token.colorPrimary,
    3: token.colorWarning,
    4: token.volcano6,
    5: token.colorError,
  }[level]);

  return (
    <Flex vertical gap={16}>
      <PageHeader
        title="工作台"
        description={greeting}
        extra={
          <Button type="primary" icon={<PlusOutlined />} disabled={!bridges?.length} onClick={() => setPickingBridge(true)}>
            新建年度检测
          </Button>
        }
      />

      {error ? (
        <Alert type="error" showIcon title={error} action={<Button type="link" size="small" onClick={() => void load()}>重新加载</Button>} />
      ) : null}

      <Row gutter={[12, 12]} role="group" aria-label="工作概况">
        {metrics.map((metric) => (
          <Col key={metric.title} xs={24} sm={12} xl={6}>
            {bridges === null ? (
              <Card><Skeleton active paragraph={{ rows: 1 }} /></Card>
            ) : (
              <MetricCard
                title={metric.title}
                value={metric.value}
                suffix={metric.suffix}
                description={metric.description}
                tone={metric.value > 0 ? metric.tone : "neutral"}
                icon={metric.icon}
              />
            )}
          </Col>
        ))}
      </Row>

      <Row gutter={[16, 16]} align="stretch">
        <Col xs={24} xl={16}>
          <Card
            role="region"
            aria-label="待处理的桥梁"
            title={
              <Space size={8}>
                <span>待处理的桥梁</span>
                {dashboard.pending.length > 0 ? (
                  <Typography.Text type="secondary" style={{ fontWeight: "normal" }}>
                    {dashboard.pending.length} 座 · 共 {dashboard.pendingTotal.toLocaleString()} 项
                  </Typography.Text>
                ) : null}
              </Space>
            }
            style={{ height: "100%" }}
          >
            {bridges === null ? <Skeleton active /> : (
              <Flex vertical gap={12}>
                <Table<BridgeSummary>
                  rowKey="id"
                  size="middle"
                  columns={pendingColumns}
                  dataSource={dashboard.pending}
                  pagination={dashboard.pending.length > kPendingPageSize ? { pageSize: kPendingPageSize, size: "small" } : false}
                  scroll={{ x: 720 }}
                  locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="当前没有待处理事项" /> }}
                />
                <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                  待处理包括还没解析完或还没校对完的导入资料，以及已确认年度里还没整理成线索的病害。
                </Typography.Text>
              </Flex>
            )}
          </Card>
        </Col>

        <Col xs={24} xl={8}>
          <Card
            role="region"
            aria-label="技术状况等级分布"
            title="技术状况等级分布"
            style={{ height: "100%" }}
          >
            {bridges === null ? <Skeleton active /> : (
              <Flex vertical gap={16}>
                <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                  按每座桥最近一次已确认的评定，共 {dashboard.total} 座
                </Typography.Text>
                <Flex vertical gap={10}>
                  {dashboard.grades.map((item) => (
                    <Flex key={item.level ?? "none"} align="center" gap={12}>
                      <Typography.Text type={item.level === null ? "secondary" : undefined} style={{ width: 48, flex: "none" }}>
                        {item.level === null ? "未评定" : `${item.level}类`}
                      </Typography.Text>
                      <Progress
                        aria-label={`${item.level === null ? "未评定" : `${item.level}类`} ${item.count} 座`}
                        percent={(item.count / maxGradeCount) * 100}
                        showInfo={false}
                        strokeColor={gradeColor(item.level)}
                        railColor={token.colorFillTertiary}
                        size={{ height: 10 }}
                        style={{ flex: 1, margin: 0 }}
                      />
                      <Typography.Text style={{ width: 44, flex: "none", textAlign: "right" }}>{item.count} 座</Typography.Text>
                    </Flex>
                  ))}
                </Flex>

                <Divider style={{ margin: 0 }} />

                <Flex vertical gap={8}>
                  <Flex align="baseline" justify="space-between" gap={8} wrap>
                    <Typography.Text strong>需重点关注</Typography.Text>
                    <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>3 类及以下，按得分从低到高</Typography.Text>
                  </Flex>
                  {dashboard.attention.length === 0 ? (
                    <Typography.Text type="success"><CheckCircleOutlined /> 没有 3 类及以下的桥梁</Typography.Text>
                  ) : (
                    <>
                      {dashboard.attention.slice(0, kAttentionLimit).map(({ bridge, level }) => (
                        <Flex key={bridge.id} align="center" gap={10}>
                          <GradeTag level={level} />
                          <Flex vertical style={{ flex: 1, minWidth: 0 }}>
                            <Typography.Link ellipsis onClick={() => navigate(bridgeOverviewPath(bridge.id))}>
                              {bridge.bridge_name}
                            </Typography.Link>
                            <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                              {[`${bridge.latest_inspection_year} 年评定`, formatScore(bridge.latest_overall_score)].filter(Boolean).join(" · ")}
                            </Typography.Text>
                          </Flex>
                          <Typography.Text type="secondary">{bridge.system_number}</Typography.Text>
                        </Flex>
                      ))}
                      {dashboard.attention.length > kAttentionLimit ? (
                        <Typography.Text type="secondary">另有 {dashboard.attention.length - kAttentionLimit} 座</Typography.Text>
                      ) : null}
                    </>
                  )}
                </Flex>
              </Flex>
            )}
          </Card>
        </Col>
      </Row>

      {/* 年度检测挂在某座桥下面：先选桥，再走和年度检测页一样的新建弹窗。 */}
      <Modal
        open={pickingBridge}
        title="新建年度检测"
        okText="下一步"
        cancelText="取消"
        okButtonProps={{ disabled: !pickedBridgeId }}
        onOk={() => {
          setCreatingFor(pickedBridgeId);
          setPickingBridge(false);
        }}
        onCancel={() => setPickingBridge(false)}
        destroyOnHidden
      >
        <Form layout="vertical">
          <Form.Item label="桥梁" htmlFor="workbench-create-bridge" style={{ marginBottom: 0 }}>
            <Select
              id="workbench-create-bridge"
              showSearch
              placeholder="搜索桥名或编号"
              value={pickedBridgeId ?? undefined}
              onChange={setPickedBridgeId}
              optionFilterProp="label"
              options={(bridges ?? []).map((bridge) => ({
                value: bridge.id,
                label: `${bridge.bridge_name}（${bridge.system_number}）`,
              }))}
            />
          </Form.Item>
        </Form>
      </Modal>

      {creatingFor ? (
        <CreateInspectionDialog
          bridgeId={creatingFor}
          onClose={() => setCreatingFor(null)}
          onCreated={(inspectionYearId) => {
            const bridgeId = creatingFor;
            setCreatingFor(null);
            navigate(inspectionWorkspacePath(bridgeId, inspectionYearId));
          }}
        />
      ) : null}
    </Flex>
  );
}

function rank(scale: string) {
  const index = kScaleOrder.indexOf(scale);
  return index === -1 ? kScaleOrder.length : index;
}
