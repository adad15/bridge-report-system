import {
  AuditOutlined,
  CalendarOutlined,
  DeleteOutlined,
  DownOutlined,
  FileSearchOutlined,
  PlusOutlined,
  ReloadOutlined,
  SafetyCertificateOutlined,
} from "@ant-design/icons";
import { Alert, Button, Card, Col, Dropdown, Empty, Flex, Input, Pagination, Row, Select, Space, Table, Tag, Typography, theme, type TableProps } from "antd";
import { useCallback, useEffect, useMemo, useState } from "react";
import { useNavigate, useSearchParams } from "react-router-dom";
import { ApiError } from "../api/apiClient";
import { type BridgeSummary, fetchBridges } from "../api/navigationApi";
import { useAuth } from "../auth/AuthContext";
import { CreateBridgeDialog } from "../bridges/CreateBridgeDialog";
import { DeleteBridgesDialog } from "../bridges/DeleteBridgesDialog";
import { backendBaseUrl } from "../config";
import { BridgeMark, MetricCard, PageHeader } from "../design-system";
import { StandardsAdminPanel } from "../standards/StandardsAdminPanel";

const currentYear = new Date().getFullYear();

function statusColor(status: string): string {
  if (status === "在用") return "green";
  if (status.includes("停用") || status.includes("废弃")) return "default";
  if (status.includes("维修") || status.includes("封闭")) return "gold";
  return "blue";
}

export function BridgesPage() {
  const [bridges, setBridges] = useState<BridgeSummary[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [query, setQuery] = useState("");
  const [routeFilter, setRouteFilter] = useState<string | undefined>();
  const [statusFilter, setStatusFilter] = useState<string | undefined>();
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [createOpen, setCreateOpen] = useState(false);
  const [deleteOpen, setDeleteOpen] = useState(false);
  const [standardsOpen, setStandardsOpen] = useState(false);
  const [page, setPage] = useState(1);
  const [searchParams, setSearchParams] = useSearchParams();
  const navigate = useNavigate();
  const { user } = useAuth();
  const { token } = theme.useToken();
  const isAdmin = user?.role === "admin";

  const reload = useCallback(async () => {
    try {
      const result = await fetchBridges(backendBaseUrl);
      setBridges(result);
      setSelected((current) => new Set([...current].filter((id) => result.some((bridge) => bridge.id === id))));
      setError(null);
    } catch (caught) {
      setBridges(null);
      setError(caught instanceof ApiError ? caught.message : "加载桥梁列表失败");
    }
  }, []);

  useEffect(() => { void reload(); }, [reload]);
  useEffect(() => {
    if (isAdmin && searchParams.get("standards") === "1") setStandardsOpen(true);
  }, [isAdmin, searchParams]);

  const visible = useMemo(() => {
    if (bridges === null) return [];
    const normalized = query.trim().toLocaleLowerCase();
    return bridges.filter((bridge) => {
      const matchesQuery = !normalized || [bridge.bridge_name, bridge.system_number, bridge.route_name ?? ""]
        .some((value) => value.toLocaleLowerCase().includes(normalized));
      return matchesQuery && (!routeFilter || bridge.route_name === routeFilter) && (!statusFilter || bridge.status === statusFilter);
    });
  }, [bridges, query, routeFilter, statusFilter]);
  const pageSize = 10;
  const pagedVisible = visible.slice((page - 1) * pageSize, page * pageSize);

  const routes = useMemo(() => [...new Set((bridges ?? []).flatMap((bridge) => bridge.route_name ? [bridge.route_name] : []))].sort((a, b) => a.localeCompare(b, "zh-CN")), [bridges]);
  const statuses = useMemo(() => [...new Set((bridges ?? []).map((bridge) => bridge.status))], [bridges]);
  const pendingCount = (bridges ?? []).reduce((sum, bridge) => sum + bridge.pending_count, 0);
  const inspectionCount = (bridges ?? []).filter((bridge) => bridge.latest_inspection_year === currentYear).length;

  function clearFilters() {
    setQuery(""); setRouteFilter(undefined); setStatusFilter(undefined); setSelected(new Set()); setPage(1);
  }
  function closeStandards() {
    setStandardsOpen(false);
    if (searchParams.has("standards")) setSearchParams({}, { replace: true });
  }
  const selectionChanged = useCallback(() => { setSelected(new Set()); setDeleteOpen(false); void reload(); }, [reload]);

  const columns: TableProps<BridgeSummary>["columns"] = [
    { title: "系统编号", dataIndex: "system_number", width: 138 },
    { title: "桥名", dataIndex: "bridge_name", render: (value: string, bridge) => <Button type="link" size="small" onClick={() => navigate(`/bridges/${encodeURIComponent(bridge.id)}`)}>{value}</Button> },
    { title: "路线", dataIndex: "route_name", width: 120, render: (value) => value ?? "—" },
    { title: "规模", dataIndex: "bridge_scale", width: 100, render: (value) => value ?? "—" },
    { title: "状态", dataIndex: "status", width: 100, render: (value: string) => <Tag color={statusColor(value)}>{value}</Tag> },
    { title: "最新结论", key: "latest_conclusion", width: 180, render: (_, bridge) => bridge.latest_inspection_year ? <span>{bridge.latest_inspection_year} · {bridge.latest_overall_grade ?? "—"}</span> : <Typography.Text type="secondary">尚未检测</Typography.Text> },
    { title: "待办", dataIndex: "pending_count", width: 80, align: "center", render: (value: number) => value > 0 ? <Tag color="gold" variant="filled">{value}</Tag> : <Typography.Text type="secondary">—</Typography.Text> },
    {
      title: "操作",
      key: "action",
      width: 240,
      render: (_, bridge) => {
        const bridgePath = `/bridges/${encodeURIComponent(bridge.id)}`;
        return (
          <Space size={0}>
            <Button type="link" icon={<FileSearchOutlined />} onClick={() => navigate(bridgePath)}>查看档案</Button>
            {isAdmin ? <Button type="link" onClick={() => navigate(bridgePath)}>编辑</Button> : null}
            <Dropdown
              trigger={["click"]}
              menu={{
                items: [
                  { key: "inspections", label: "年度检测" },
                  { key: "components", label: "构件档案" },
                ],
                onClick: ({ key }) => navigate(`${bridgePath}/${key}`),
              }}
            >
              <Button type="link">更多 <DownOutlined /></Button>
            </Dropdown>
          </Space>
        );
      },
    },
  ];
  const rowSelection: TableProps<BridgeSummary>["rowSelection"] | undefined = isAdmin ? {
    selectedRowKeys: [...selected],
    onChange: (keys) => setSelected(new Set(keys.map(String))),
    getCheckboxProps: (record) => ({ "aria-label": `选择 ${record.system_number}` }),
  } : undefined;

  const metrics = [
    { key: "all", title: "全部桥梁", value: bridges?.length ?? 0, icon: <BridgeMark color={token.colorPrimary} width={34} />, tone: "primary" as const },
    { key: "active", title: "在用桥梁", value: (bridges ?? []).filter((bridge) => bridge.status === "在用").length, icon: <SafetyCertificateOutlined />, tone: "success" as const },
    { key: "pending", title: "待处理事项", value: pendingCount, icon: <AuditOutlined />, tone: "warning" as const },
    { key: "year", title: "本年度检测", value: inspectionCount, icon: <CalendarOutlined />, tone: "primary" as const },
  ];

  return (
    <Flex vertical gap={16}>
      <PageHeader
        title="桥梁档案"
        description="统一管理桥梁基础信息、年度检测与跨年病害档案。"
        extra={isAdmin ? (
          <>
            <Button aria-label="规范管理" icon={<SafetyCertificateOutlined />} onClick={() => setStandardsOpen(true)}>规范管理</Button>
            <Button aria-label="添加桥梁" type="primary" icon={<PlusOutlined />} onClick={() => setCreateOpen(true)}>添加桥梁</Button>
          </>
        ) : null}
      />

      <Row gutter={[16, 16]} role="group" aria-label="桥梁档案概况">
        {metrics.map(({ key, ...metric }) => (
          <Col key={key} xs={24} sm={12} xl={6}>
            <MetricCard {...metric} />
          </Col>
        ))}
      </Row>

      <Card title={<Space>桥梁列表<Tag color="cyan">共 {bridges?.length ?? 0} 座</Tag></Space>}>
        <Flex vertical gap={16}>
          <Row gutter={[12, 12]}>
            <Col xs={24} lg={isAdmin ? 9 : 12}>
              <Input.Search aria-label="搜索桥名、编号或路线" value={query} onChange={(event) => { setQuery(event.target.value); setSelected(new Set()); setPage(1); }} placeholder="搜索桥名、编号或路线" allowClear />
            </Col>
            <Col xs={12} lg={4}>
              <Select aria-label="按路线筛选" value={routeFilter} onChange={(value) => { setRouteFilter(value); setSelected(new Set()); setPage(1); }} placeholder="全部路线" allowClear options={routes.map((route) => ({ label: route, value: route }))} style={{ width: "100%" }} />
            </Col>
            <Col xs={12} lg={4}>
              <Select aria-label="按状态筛选" value={statusFilter} onChange={(value) => { setStatusFilter(value); setSelected(new Set()); setPage(1); }} placeholder="全部状态" allowClear options={statuses.map((status) => ({ label: status, value: status }))} style={{ width: "100%" }} />
            </Col>
            <Col xs={12} lg={isAdmin ? 3 : 4}>
              <Button block icon={<ReloadOutlined />} onClick={clearFilters}>重置</Button>
            </Col>
            {isAdmin ? (
              <Col xs={12} lg={4}>
                <Button block aria-label={`删除选中桥梁（${selected.size}）`} icon={<DeleteOutlined />} disabled={selected.size === 0} onClick={() => setDeleteOpen(true)}>删除选中桥梁（{selected.size}）</Button>
              </Col>
            ) : null}
          </Row>
          {error ? (
            <Alert type="error" showIcon title={error} action={<Button type="link" size="small" onClick={() => void reload()}>重新加载</Button>} />
          ) : null}
          <Table<BridgeSummary>
            rowKey="id"
            loading={bridges === null && error === null}
            columns={columns}
            dataSource={pagedVisible}
            rowSelection={rowSelection}
            pagination={false}
            scroll={{ x: 1120 }}
            locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={bridges?.length ? "没有匹配的桥梁" : "暂无桥梁档案"} /> }}
            onRow={(bridge) => ({ onDoubleClick: () => navigate(`/bridges/${encodeURIComponent(bridge.id)}`) })}
          />
          <Flex align="center" justify="space-between" gap={16} wrap>
            <Typography.Text type="secondary">共 {visible.length} 条</Typography.Text>
            <Pagination current={page} pageSize={pageSize} total={visible.length} showSizeChanger={false} hideOnSinglePage={false} onChange={setPage} />
          </Flex>
        </Flex>
      </Card>

      {standardsOpen ? <StandardsAdminPanel onClose={closeStandards} /> : null}
      {createOpen ? <CreateBridgeDialog onClose={() => setCreateOpen(false)} onCreated={(bridge) => { setCreateOpen(false); navigate(`/bridges/${encodeURIComponent(bridge.id)}`); }} /> : null}
      {deleteOpen ? <DeleteBridgesDialog bridgeIds={[...selected]} onClose={() => { setDeleteOpen(false); setSelected(new Set()); void reload(); }} onSelectionChanged={selectionChanged} onCompleted={() => { setSelected(new Set()); void reload(); }} /> : null}
    </Flex>
  );
}
