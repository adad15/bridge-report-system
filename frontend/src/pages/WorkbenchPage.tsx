import {
  ApartmentOutlined,
  AuditOutlined,
  BarChartOutlined,
  CheckCircleOutlined,
  ClockCircleOutlined,
  FileDoneOutlined,
  FileProtectOutlined,
  FolderOpenOutlined,
  ImportOutlined,
  PlayCircleOutlined,
  PlusOutlined,
  SafetyCertificateOutlined,
  UploadOutlined,
  WarningOutlined,
} from "@ant-design/icons";
import { Button, Card, Checkbox, Empty, Skeleton, Statistic, Steps, Tabs, Tag } from "antd";
import { useCallback, useEffect, useMemo, useState, type ReactNode } from "react";
import { useNavigate } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import { fetchBridges, type BridgeSummary } from "../api/navigationApi";
import { useAuth } from "../auth/AuthContext";
import { backendBaseUrl } from "../config";
import "./WorkbenchPage.css";

interface DashboardMetric {
  title: string;
  value: number;
  tone: string;
  icon: ReactNode;
}

export function WorkbenchPage() {
  const navigate = useNavigate();
  const { user } = useAuth();
  const [bridges, setBridges] = useState<BridgeSummary[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [selectedTasks, setSelectedTasks] = useState<Set<string>>(new Set());

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

  const dashboard = useMemo(() => {
    const source = bridges ?? [];
    const currentYear = new Date().getFullYear();
    const pendingCount = source.reduce((sum, bridge) => sum + bridge.pending_count, 0);
    const currentInspections = source.filter(
      (bridge) => bridge.latest_inspection_year === currentYear,
    );
    return {
      currentYear,
      pendingCount,
      currentInspections,
      tasks: source.filter((bridge) => bridge.pending_count > 0).slice(0, 5),
      recent: source
        .filter((bridge) => bridge.latest_inspection_year !== null)
        .sort((left, right) =>
          (right.latest_inspection_year ?? 0) - (left.latest_inspection_year ?? 0),
        )
        .slice(0, 4),
    };
  }, [bridges]);

  const metrics: DashboardMetric[] = [
    {
      title: "待处理任务",
      value: dashboard.pendingCount,
      tone: "orange",
      icon: <AuditOutlined />,
    },
    {
      title: "进行中检测",
      value: dashboard.currentInspections.length,
      tone: "blue",
      icon: <ClockCircleOutlined />,
    },
    {
      title: "待校对资料",
      value: 0,
      tone: "indigo",
      icon: <FileDoneOutlined />,
    },
    {
      title: "待生成报告",
      value: 0,
      tone: "royal",
      icon: <BarChartOutlined />,
    },
  ];

  function openInspectionWorkspace() {
    const target = bridges?.[0];
    navigate(target ? `/bridges/${encodeURIComponent(target.id)}/inspections` : "/bridges");
  }

  const dayPeriod = new Date().getHours() < 12 ? "上午好" : new Date().getHours() < 18 ? "下午好" : "晚上好";

  function renderTaskList(items: BridgeSummary[]) {
    if (items.length === 0) {
      return <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="当前没有匹配的待办" />;
    }
    return (
      <div className="workbench-task-list">
        {items.map((bridge) => (
          <div key={bridge.id} className="workbench-task-row">
            <Checkbox
              aria-label={`选择 ${bridge.bridge_name} 待办`}
              checked={selectedTasks.has(bridge.id)}
              onChange={(event) => setSelectedTasks((current) => {
                const next = new Set(current);
                if (event.target.checked) next.add(bridge.id); else next.delete(bridge.id);
                return next;
              })}
            />
            <span className="workbench-task-bridge-mark" aria-hidden="true">
              <span />
            </span>
            <div className="workbench-task-main">
              <div className="workbench-task-title">
                <Tag color="orange" variant="filled">高</Tag>
                <strong>完善{bridge.bridge_name}基础档案</strong>
              </div>
              <div className="workbench-task-meta">
                <Tag variant="filled">档案完整性</Tag>
                <span>缺少桥梁技术状况与管养单位信息</span>
              </div>
            </div>
            <Tag color="orange" variant="filled">建议今天处理</Tag>
            <Button type="link" onClick={() => navigate(`/bridges/${encodeURIComponent(bridge.id)}`)}>去处理</Button>
          </div>
        ))}
      </div>
    );
  }

  const taskTabs = [
    { key: "all", label: <>全部 <span>{dashboard.tasks.length}</span></>, children: renderTaskList(dashboard.tasks) },
    { key: "priority", label: <>高优先级 <span>{dashboard.tasks.length}</span></>, children: renderTaskList(dashboard.tasks) },
    { key: "due", label: <>即将到期 <span>0</span></>, children: renderTaskList([]) },
  ];

  const workflowItems = [
    { title: "创建检测", content: "0", icon: <PlusOutlined />, status: "wait" as const },
    { title: "导入资料", content: "0", icon: <UploadOutlined />, status: "wait" as const },
    { title: "数据校对", content: "0", icon: <FileProtectOutlined />, status: "wait" as const },
    { title: "系统评定", content: "0", icon: <SafetyCertificateOutlined />, status: "wait" as const },
    { title: "生成报告", content: "0", icon: <BarChartOutlined />, status: "wait" as const },
  ];

  const primaryTask = dashboard.tasks[0] ?? bridges?.[0] ?? null;
  const recentBridge = dashboard.recent[0] ?? bridges?.[0] ?? null;

  return (
    <section className="workbench-page">
      <header className="workspace-page-header workbench-page-header">
        <div className="workbench-page-heading">
          <div className="workbench-welcome-line">
            <h1>工作台</h1>
            <span className="workspace-heading-divider" aria-hidden="true" />
            <div>
              <strong>{dayPeriod}，{user?.display_name}</strong>
              <p>集中处理年度检测、资料校对和报告生成任务。</p>
            </div>
          </div>
        </div>
        <div className="workspace-page-actions">
          <Button type="primary" icon={<PlusOutlined />} onClick={openInspectionWorkspace}>
            创建年度检测
          </Button>
          <Button icon={<ImportOutlined />} onClick={openInspectionWorkspace}>导入检测资料</Button>
        </div>
      </header>

      {error ? (
        <div className="workbench-error" role="alert">
          <span>{error}</span>
          <Button type="link" onClick={() => void load()}>重新加载</Button>
        </div>
      ) : null}

      <div className="workbench-metrics" aria-label="工作概况">
        {metrics.map((metric) => (
          <Card
            key={metric.title}
            className={`workbench-metric workbench-metric-${metric.tone}`}
            styles={{ body: { padding: "18px 20px" } }}
          >
            {bridges === null ? <Skeleton active paragraph={false} /> : (
              <div className="workbench-metric-content">
                <span className="workbench-metric-icon">{metric.icon}</span>
                <div>
                  <span className="workbench-metric-title">{metric.title}</span>
                  <Statistic value={metric.value} styles={{ content: { color: "#172033", fontWeight: 700, lineHeight: 1.1 } }} />
                </div>
              </div>
            )}
          </Card>
        ))}
      </div>

      <div className="workbench-dashboard-grid">
        <Card
          className="workspace-card workbench-tasks-card"
          title="我的待办"
          styles={{ header: { minHeight: 48, padding: "0 20px" }, body: { padding: "0 16px 16px" } }}
        >
          {bridges === null ? <Skeleton active /> : <Tabs items={taskTabs} classNames={{ header: "workbench-task-tabs-header", item: "workbench-task-tab", indicator: "workbench-task-tab-indicator", body: "workbench-task-tabs-body" }} />}
        </Card>

        <Card className="workspace-card workbench-process-card" title="年度检测流程" styles={{ header: { minHeight: 48, padding: "0 20px", textAlign: "left" }, body: { padding: "18px 22px 16px", textAlign: "center" } }}>
          <Steps
            orientation="horizontal"
            titlePlacement="vertical"
            responsive={false}
            current={-1}
            items={workflowItems}
            classNames={{ root: "workbench-flow", item: "workbench-flow-item", itemIcon: "workbench-flow-icon", itemTitle: "workbench-flow-title", itemContent: "workbench-flow-count", itemRail: "workbench-flow-rail" }}
          />
          <p className="workbench-flow-empty">当前没有进行中的年度检测</p>
          <Button className="workbench-flow-action" icon={<PlayCircleOutlined />} onClick={openInspectionWorkspace}>开始一次年度检测</Button>
        </Card>
      </div>

      <div className="workbench-lower-grid">
        <div className="workbench-lower-left">
          <Card className="workspace-card workbench-running-card" title="进行中的年度检测" styles={{ header: { minHeight: 48, padding: "0 20px" }, body: { padding: "12px 20px 18px" } }}>
            {dashboard.currentInspections.length === 0 ? (
              <div className="workbench-running-empty">
                <div className="workbench-empty-illustration" aria-hidden="true">
                  <span className="workbench-empty-leaf workbench-empty-leaf-left" />
                  <span className="workbench-empty-leaf workbench-empty-leaf-right" />
                  <span className="workbench-empty-board"><BarChartOutlined /></span>
                </div>
                <strong>暂无进行中的年度检测</strong>
                <p>创建后可在这里持续查看资料、校对与报告进度</p>
                <Button type="primary" icon={<PlusOutlined />} onClick={openInspectionWorkspace}>创建年度检测</Button>
              </div>
            ) : (
              <div className="workbench-compact-list">
                {dashboard.currentInspections.slice(0, 4).map((bridge) => (
                  <button key={bridge.id} type="button" onClick={() => navigate(`/bridges/${bridge.id}/inspections`)}>
                    <span><CheckCircleOutlined /> {bridge.bridge_name}</span>
                    <small>{bridge.latest_inspection_year} 年 · {bridge.latest_overall_grade ?? "评定中"}</small>
                  </button>
                ))}
              </div>
            )}
          </Card>

          <div className="workbench-quick-actions" aria-label="常用操作">
            <strong>常用操作</strong>
            <Button type="text" icon={<ImportOutlined />} onClick={openInspectionWorkspace}>导入检测资料</Button>
            <span />
            <Button type="text" icon={<ApartmentOutlined />} onClick={() => navigate("/rating-trees")}>查看评定树</Button>
            <span />
            {user?.role === "admin" ? <Button type="text" icon={<FolderOpenOutlined />} onClick={() => navigate("/bridges?standards=1")}>规范管理</Button> : null}
          </div>
        </div>

        <div className="workbench-lower-right">
          <Card className="workspace-card workbench-alert-card" title="异常与提醒" styles={{ header: { minHeight: 48, padding: "0 20px" }, body: { padding: "12px 18px" } }}>
            {primaryTask ? (
              <div className="workbench-alert-row">
                <span className="workbench-alert-icon"><WarningOutlined /></span>
                <span>{primaryTask.bridge_name}基础档案信息不完整</span>
                <Button type="link" onClick={() => navigate(`/bridges/${encodeURIComponent(primaryTask.id)}`)}>查看</Button>
              </div>
            ) : (
              <div className="workbench-alert-row workbench-alert-row-clear"><CheckCircleOutlined /><span>当前没有异常提醒</span></div>
            )}
          </Card>

          <Card className="workspace-card workbench-recent-card" title="最近动态" styles={{ header: { minHeight: 48, padding: "0 20px" }, body: { padding: "12px 20px 16px" } }}>
            {recentBridge ? (
              <div className="workbench-recent-row">
                <span className="workbench-recent-dot" />
                <div><p>创建桥梁档案：{recentBridge.bridge_name}</p><small>今天 09:30</small></div>
              </div>
            ) : <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="暂无业务动态" />}
          </Card>
        </div>
      </div>
    </section>
  );
}
