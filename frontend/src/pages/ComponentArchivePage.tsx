import {
  ApartmentOutlined,
  FolderOpenOutlined,
  LinkOutlined,
  WarningOutlined,
} from "@ant-design/icons";
import { Alert, Button, Card, Col, Empty, Flex, Grid, Row, Skeleton, Typography } from "antd";
import { useEffect, useState } from "react";
import { useNavigate, useParams } from "react-router-dom";

import type { ArchiveObservation, ComponentDefectArchive, ComponentSummary } from "../api/componentArchiveApi";
import { fetchComponentArchive, fetchComponents } from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { ComponentDetailPanel } from "../archive/ComponentDetailPanel";
import { ComponentListPanel } from "../archive/ComponentListPanel";
import { RebindDialog } from "../archive/RebindDialog";
import { backendBaseUrl } from "../config";
import { MetricCard, useShellMetrics, type MetricTone } from "../design-system";

interface ArchiveZeroStateProps {
  components: ComponentSummary[] | null;
  onSelect: (componentId: string) => void;
}

/**
 * 未选构件时的右侧面板。
 *
 * 这里原来摆着"导入检测资料"和"查看构件台账"两个按钮，但都不是这个页面该发起的动作：
 * 导入属于年度检测，台账是另一条业务线。删掉之后剩下大片空白，所以改成把**最该先看的
 * 构件**摆出来——这一页的用途就是找有问题的构件，空态直接给答案比让人自己去左栏翻更省事。
 */
function ArchiveZeroState({ components, onSelect }: ArchiveZeroStateProps) {
  const worst = (components ?? [])
    .filter((component): component is ComponentSummary & { latest_score: number } =>
      component.latest_score !== null)
    .sort((left, right) => left.latest_score - right.latest_score)
    .slice(0, 8);

  return (
    <Flex vertical align="center" gap={12} style={{ paddingBlock: 24 }}>
      <Empty
        image={Empty.PRESENTED_IMAGE_SIMPLE}
        description={
          <Flex vertical gap={4}>
            <Typography.Title level={4} style={{ margin: 0 }}>请选择一个构件</Typography.Title>
            <Typography.Text type="secondary">从左侧选择构件，查看历年病害、照片与跨年变化。</Typography.Text>
          </Flex>
        }
      />
      {worst.length > 0 ? (
        <Flex vertical gap={10} style={{ width: "100%", maxWidth: 720 }}>
          <Typography.Title level={5} style={{ margin: 0, textAlign: "center" }}>评分最低的构件</Typography.Title>
          <Row gutter={[8, 8]}>
            {worst.map((component) => (
              <Col key={component.id} xs={12} md={8} xl={6}>
                <Button block onClick={() => onSelect(component.id)} style={{ paddingInline: 10 }}>
                  <Flex align="center" justify="space-between" gap={8} style={{ width: "100%" }}>
                    <Typography.Text ellipsis>{component.business_component_code}</Typography.Text>
                    <Typography.Text type="secondary" style={{ flex: "none" }}>
                      {Math.round((component.latest_score + Number.EPSILON) * 100) / 100}
                    </Typography.Text>
                  </Flex>
                </Button>
              </Col>
            ))}
          </Row>
        </Flex>
      ) : null}
      <Typography.Text type="secondary">
        <LinkOutlined /> 跨年度病害会根据构件绑定与病害特征生成追踪线索
      </Typography.Text>
    </Flex>
  );
}

// 模块 06 构件病害档案主页面（A1 布局）：概况指标 + 整理提示 + 左侧构件列表 / 右侧档案详情。
// 只读优先：本页不修改任何年度事实；绑定/重绑经线索整理页或观测行入口发起。
export function ComponentArchivePage() {
  const { bridgeId, componentId } = useParams<{ bridgeId: string; componentId?: string }>();
  const navigate = useNavigate();
  const screens = Grid.useBreakpoint();
  const { pageOffset } = useShellMetrics();

  const [components, setComponents] = useState<ComponentSummary[] | null>(null);
  const [listError, setListError] = useState<string | null>(null);
  const [archive, setArchive] = useState<ComponentDefectArchive | null>(null);
  const [archiveError, setArchiveError] = useState<string | null>(null);
  const [rebindTarget, setRebindTarget] = useState<ArchiveObservation | null>(null);
  // 绑定成功后触发档案重载。
  const [archiveVersion, setArchiveVersion] = useState(0);

  useEffect(() => {
    if (!bridgeId) return;
    let cancelled = false;
    fetchComponents(backendBaseUrl, bridgeId)
      .then((items) => {
        if (cancelled) return;
        setComponents(items);
        setListError(null);
      })
      .catch((error) => {
        if (cancelled) return;
        setListError(error instanceof ApiError ? error.message : "构件列表加载失败。");
      });
    return () => {
      cancelled = true;
    };
  }, [bridgeId]);

  useEffect(() => {
    if (!componentId) {
      setArchive(null);
      return;
    }
    let cancelled = false;
    setArchive(null);
    setArchiveError(null);
    fetchComponentArchive(backendBaseUrl, componentId)
      .then((body) => {
        if (cancelled) return;
        setArchive(body);
        setArchiveError(null);
      })
      .catch((error) => {
        if (cancelled) return;
        setArchiveError(error instanceof ApiError ? error.message : "构件档案加载失败。");
      });
    return () => {
      cancelled = true;
    };
  }, [componentId, archiveVersion]);

  if (!bridgeId) {
    return <Alert type="error" showIcon title="缺少桥梁标识。" />;
  }

  const unboundCount = components?.reduce((total, component) => total + component.unbound_count, 0) ?? 0;
  const componentCount = components?.length ?? 0;
  const threadCount = components?.reduce((total, component) => total + component.thread_count, 0) ?? 0;
  const crossYearCount = components?.filter((component) => component.first_seen_year !== component.latest_seen_year).length ?? 0;
  const firstImportedYear = components?.length
    ? Math.min(...components.map((component) => component.first_seen_year))
    : null;
  const latestImportedYear = components?.length
    ? Math.max(...components.map((component) => component.latest_seen_year))
    : null;
  const importedYearCount = firstImportedYear !== null && latestImportedYear !== null
    ? latestImportedYear - firstImportedYear + 1
    : 0;

  const metrics: Array<{ label: string; value: number; tone: MetricTone; icon: JSX.Element }> = [
    { label: "病害构件", value: componentCount, tone: "primary", icon: <ApartmentOutlined /> },
    { label: "病害线索", value: threadCount, tone: "warning", icon: <WarningOutlined /> },
    { label: "跨年构件", value: crossYearCount, tone: "success", icon: <LinkOutlined /> },
    { label: "待整理观测", value: unboundCount, tone: "error", icon: <FolderOpenOutlined /> },
  ];

  // 窄屏两栏上下叠放，交还给整页滚动。
  const stacked = screens.lg === false;
  const panelStyle = stacked ? undefined : { height: "100%" };

  return (
    /* 两栏吃满顶栏以下的视口，左右各自滚动，页面本身不滚。 */
    <Flex vertical gap={16} style={stacked ? undefined : { height: `calc(100dvh - ${pageOffset}px)` }}>
      <Row gutter={[12, 12]} role="group" aria-label="构件病害档案概况">
        {metrics.map((metric) => (
          <Col key={metric.label} xs={12} xl={6}>
            <MetricCard
              size="small"
              title={metric.label}
              value={metric.value}
              tone={metric.value > 0 ? metric.tone : "neutral"}
              icon={metric.icon}
            />
          </Col>
        ))}
      </Row>

      {unboundCount > 0 ? (
        <Alert
          type="info"
          showIcon
          role="note"
          title={`${importedYearCount} 个年度已导入，${unboundCount} 条病害观测尚未整理为跨年线索`}
          description="可先查看单个构件的年度记录，再进入线索整理批量确认。"
          action={
            <Button type="primary" href={`/bridges/${bridgeId}/defect-threads/triage`}>
              开始线索整理
            </Button>
          }
        />
      ) : null}

      <Row gutter={[14, 14]} style={stacked ? undefined : { flex: 1, minHeight: 0 }}>
        <Col xs={24} lg={9} xxl={7} style={panelStyle}>
          <Card
            title="病害构件"
            extra={<Typography.Text type="secondary">{componentCount}</Typography.Text>}
            style={{ height: stacked ? undefined : "100%" }}
            styles={{
              root: { display: "flex", flexDirection: "column" },
              body: { flex: 1, minHeight: 0, display: "flex", flexDirection: "column" },
            }}
          >
            {listError ? (
              <Alert type="error" showIcon title={listError} />
            ) : components === null ? (
              <Skeleton active paragraph={{ rows: 8 }} />
            ) : components.length === 0 ? (
              <Empty
                image={Empty.PRESENTED_IMAGE_SIMPLE}
                description={
                  <Flex vertical gap={4}>
                    <Typography.Text>暂无病害构件</Typography.Text>
                    <Typography.Text type="secondary">导入并确认年度检测后，可在这里按构件筛选。</Typography.Text>
                  </Flex>
                }
              />
            ) : (
              <ComponentListPanel
                components={components}
                selectedComponentId={componentId ?? null}
                onSelect={(nextId) => navigate(`/bridges/${bridgeId}/components/${nextId}`)}
              />
            )}
          </Card>
        </Col>

        <Col xs={24} lg={15} xxl={17} style={panelStyle}>
          <Card
            style={{ height: stacked ? undefined : "100%" }}
            styles={{
              root: { display: "flex", flexDirection: "column" },
              body: { flex: 1, minHeight: 0, overflowY: "auto" },
            }}
          >
            {!componentId ? (
              <ArchiveZeroState
                components={components}
                onSelect={(nextId) => navigate(`/bridges/${bridgeId}/components/${nextId}`)}
              />
            ) : archiveError ? (
              <Alert type="error" showIcon title={archiveError} />
            ) : archive === null ? (
              <Skeleton active paragraph={{ rows: 6 }} />
            ) : (
              <ComponentDetailPanel
                archive={archive}
                bridgeId={bridgeId}
                onRebind={(observation) => setRebindTarget(observation)}
              />
            )}
          </Card>
        </Col>
      </Row>

      {rebindTarget !== null && archive !== null ? (
        <RebindDialog
          observation={rebindTarget}
          threads={archive.threads}
          onClose={() => setRebindTarget(null)}
          onSuccess={() => {
            setRebindTarget(null);
            setArchiveVersion((version) => version + 1);
          }}
        />
      ) : null}
    </Flex>
  );
}
