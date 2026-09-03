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
import { Button, Card, Dropdown, Empty, Input, Pagination, Select, Statistic, Table, Tag, type TableProps } from "antd";
import { useCallback, useEffect, useMemo, useState } from "react";
import { useNavigate, useSearchParams } from "react-router-dom";
import { ApiError } from "../api/apiClient";
import { type BridgeSummary, fetchBridges } from "../api/navigationApi";
import { useAuth } from "../auth/AuthContext";
import { CreateBridgeDialog } from "../bridges/CreateBridgeDialog";
import { DeleteBridgesDialog } from "../bridges/DeleteBridgesDialog";
import { backendBaseUrl } from "../config";
import { StandardsAdminPanel } from "../standards/StandardsAdminPanel";
import "./BridgesPage.css";

const currentYear = new Date().getFullYear();

function BridgeMetricMark() {
  return (
    <span className="bridge-metric-mark" aria-hidden="true">
      <span className="bridge-metric-mark-pylon" />
      <span className="bridge-metric-mark-deck" />
      <span className="bridge-metric-mark-cable bridge-metric-mark-cable-left" />
      <span className="bridge-metric-mark-cable bridge-metric-mark-cable-right" />
    </span>
  );
}

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
    { title: "系统编号", dataIndex: "system_number", width: 138, render: (value: string) => <span className="bridge-system-number">{value}</span> },
    { title: "桥名", dataIndex: "bridge_name", render: (value: string, bridge) => <button type="button" className="bridge-name-link" onClick={() => navigate(`/bridges/${encodeURIComponent(bridge.id)}`)}>{value}</button> },
    { title: "路线", dataIndex: "route_name", width: 120, render: (value) => value ?? "—" },
    { title: "规模", dataIndex: "bridge_scale", width: 100, render: (value) => value ?? "—" },
    { title: "状态", dataIndex: "status", width: 100, render: (value: string) => <Tag color={statusColor(value)}>{value}</Tag> },
    { title: "最新结论", key: "latest_conclusion", width: 180, render: (_, bridge) => bridge.latest_inspection_year ? <span>{bridge.latest_inspection_year} · {bridge.latest_overall_grade ?? "—"}</span> : <span className="bridge-table-muted">尚未检测</span> },
    { title: "待办", dataIndex: "pending_count", width: 80, align: "center", render: (value: number) => value > 0 ? <span className="bridge-pending-count">{value}</span> : <span className="bridge-table-muted">—</span> },
    {
      title: "操作",
      key: "action",
      width: 240,
      render: (_, bridge) => {
        const bridgePath = `/bridges/${encodeURIComponent(bridge.id)}`;
        return (
          <div className="bridge-table-actions">
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
          </div>
        );
      },
    },
  ];
  const rowSelection: TableProps<BridgeSummary>["rowSelection"] | undefined = isAdmin ? {
    selectedRowKeys: [...selected],
    onChange: (keys) => setSelected(new Set(keys.map(String))),
    getCheckboxProps: (record) => ({ "aria-label": `选择 ${record.system_number}` }),
  } : undefined;

  return (
    <section className="bridge-archive-page">
      <header className="workspace-page-header bridge-archive-header">
        <div className="bridge-archive-heading">
          <div className="bridge-archive-title-line">
            <h1>桥梁档案</h1>
            <span className="workspace-heading-divider" aria-hidden="true" />
            <p>统一管理桥梁基础信息、年度检测与跨年病害档案。</p>
          </div>
        </div>
        {isAdmin ? <div className="workspace-page-actions"><Button aria-label="规范管理" icon={<SafetyCertificateOutlined />} onClick={() => setStandardsOpen(true)}>规范管理</Button><Button aria-label="添加桥梁" type="primary" icon={<PlusOutlined />} onClick={() => setCreateOpen(true)}>添加桥梁</Button></div> : null}
      </header>

      <div className="bridge-archive-metrics" aria-label="桥梁档案概况">
        <Card className="bridge-archive-metric"><div className="bridge-metric-content"><span className="bridge-metric-icon bridge-metric-icon-bridge"><BridgeMetricMark /></span><Statistic title="全部桥梁" value={bridges?.length ?? 0} styles={{ title: { color: "#647187", fontSize: 13 }, content: { color: "#172033", fontWeight: 700, lineHeight: 1.1 } }} /></div></Card>
        <Card className="bridge-archive-metric"><div className="bridge-metric-content"><span className="bridge-metric-icon bridge-metric-icon-active"><SafetyCertificateOutlined /></span><Statistic title="在用桥梁" value={(bridges ?? []).filter((bridge) => bridge.status === "在用").length} styles={{ title: { color: "#647187", fontSize: 13 }, content: { color: "#172033", fontWeight: 700, lineHeight: 1.1 } }} /></div></Card>
        <Card className="bridge-archive-metric"><div className="bridge-metric-content"><span className="bridge-metric-icon bridge-metric-icon-warning"><AuditOutlined /></span><Statistic title="待处理事项" value={pendingCount} styles={{ title: { color: "#647187", fontSize: 13 }, content: { color: "#172033", fontWeight: 700, lineHeight: 1.1 } }} /></div></Card>
        <Card className="bridge-archive-metric"><div className="bridge-metric-content"><span className="bridge-metric-icon bridge-metric-icon-year"><CalendarOutlined /></span><Statistic title="本年度检测" value={inspectionCount} styles={{ title: { color: "#647187", fontSize: 13 }, content: { color: "#172033", fontWeight: 700, lineHeight: 1.1 } }} /></div></Card>
      </div>

      <Card className="bridge-archive-list-card" title={<span className="bridge-list-title">桥梁列表 <Tag color="cyan">共 {bridges?.length ?? 0} 座</Tag></span>}>
        <div className="bridge-archive-filters">
          <Input.Search aria-label="搜索桥名、编号或路线" value={query} onChange={(event) => { setQuery(event.target.value); setSelected(new Set()); setPage(1); }} placeholder="搜索桥名、编号或路线" allowClear />
          <Select aria-label="按路线筛选" value={routeFilter} onChange={(value) => { setRouteFilter(value); setSelected(new Set()); setPage(1); }} placeholder="全部路线" allowClear options={routes.map((route) => ({ label: route, value: route }))} />
          <Select aria-label="按状态筛选" value={statusFilter} onChange={(value) => { setStatusFilter(value); setSelected(new Set()); setPage(1); }} placeholder="全部状态" allowClear options={statuses.map((status) => ({ label: status, value: status }))} />
          <Button icon={<ReloadOutlined />} onClick={clearFilters}>重置</Button>
          {isAdmin ? <Button aria-label={`删除选中桥梁（${selected.size}）`} icon={<DeleteOutlined />} disabled={selected.size === 0} onClick={() => setDeleteOpen(true)}>删除选中桥梁（{selected.size}）</Button> : null}
        </div>
        {error ? <div className="bridge-archive-error" role="alert"><span>{error}</span><Button type="link" onClick={() => void reload()}>重新加载</Button></div> : null}
        <div className="bridge-archive-table-area">
          <Table<BridgeSummary> rowKey="id" loading={bridges === null && error === null} columns={columns} dataSource={pagedVisible} rowSelection={rowSelection} pagination={false} scroll={{ x: 1120 }} locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={bridges?.length ? "没有匹配的桥梁" : "暂无桥梁档案"} /> }} onRow={(bridge) => ({ className: "bridge-archive-table-row", onDoubleClick: () => navigate(`/bridges/${encodeURIComponent(bridge.id)}`) })} />
        </div>
        <footer className="bridge-archive-pagination"><span>共 {visible.length} 条</span><Pagination current={page} pageSize={pageSize} total={visible.length} showSizeChanger={false} hideOnSinglePage={false} onChange={setPage} /></footer>
      </Card>

      {standardsOpen ? <StandardsAdminPanel onClose={closeStandards} /> : null}
      {createOpen ? <CreateBridgeDialog onClose={() => setCreateOpen(false)} onCreated={(bridge) => { setCreateOpen(false); navigate(`/bridges/${encodeURIComponent(bridge.id)}`); }} /> : null}
      {deleteOpen ? <DeleteBridgesDialog bridgeIds={[...selected]} onClose={() => { setDeleteOpen(false); setSelected(new Set()); void reload(); }} onSelectionChanged={selectionChanged} onCompleted={() => { setSelected(new Set()); void reload(); }} /> : null}
    </section>
  );
}
