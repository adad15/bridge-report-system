import {
  ApartmentOutlined,
  AuditOutlined,
  FileDoneOutlined,
  HistoryOutlined,
} from "@ant-design/icons";
import { Button, Card, Col, Divider, Flex, Grid, Row, Typography, theme } from "antd";
import { useNavigate } from "react-router-dom";

import { useAuth } from "../auth/AuthContext";
import { MetricCard, type MetricTone } from "../design-system";
import { BridgeLocationCard } from "../bridges/BridgeLocationCard";
import { BridgeProfileCard } from "../bridges/BridgeProfileCard";
import { OVERVIEW_SCROLL_HEIGHT } from "../bridges/overviewLayout";
import { useBridgeMedia } from "../bridges/useBridgeMedia";
import { useBridgeProfile } from "../bridges/useBridgeProfile";

import type {
  BridgeDefectComparison,
  DefectGroupDelta,
  DefectTypeDelta,
} from "../api/workspaceApi";
import { useBridgeWorkspace } from "../workspace/BridgeWorkspaceShell";
import { inspectionsPath } from "../workspace/workspaceState";

const show = (value: string | number | null | undefined) => value ?? "—";

interface DefectComparisonCardProps {
  comparison: BridgeDefectComparison;
}

/** 源数据里的病害类型有前后空格，个别年度还录成了 "/"。展示前统一收拾。 */
function defectTypeLabel(defectType: string): string {
  const trimmed = defectType.trim();
  return trimmed === "" || trimmed === "/" || trimmed === "-" ? "未标明类型" : trimmed;
}

/** "横向裂缝 3 条、网状裂缝 1 条"。条数为 0 的类型不出现在该年度的句子里。 */
function describeTypes(types: DefectTypeDelta[], pick: (type: DefectTypeDelta) => number): string {
  return types
    .filter((type) => pick(type) > 0)
    .sort((left, right) => pick(right) - pick(left))
    .map((type) => `${defectTypeLabel(type.defect_type)} ${pick(type)} 条`)
    .join("、");
}

/** 一类构件的两年对比句：先上年、后最新年，最后给差值。 */
function describeGroup(group: DefectGroupDelta, comparison: BridgeDefectComparison): string {
  const previous = group.previous_count > 0
    ? `${comparison.previous_year} 年记录病害 ${group.previous_count} 条，为`
      + `${describeTypes(group.defect_types, (type) => type.previous_count)}`
    : `${comparison.previous_year} 年未记录病害`;
  const latest = group.latest_count > 0
    ? `${comparison.latest_year} 年记录 ${group.latest_count} 条，为`
      + `${describeTypes(group.defect_types, (type) => type.latest_count)}`
    : `${comparison.latest_year} 年未记录病害`;
  const change = group.latest_count - group.previous_count;
  const trend = change > 0 ? `较上年多 ${change} 条`
    : change < 0 ? `较上年少 ${-change} 条` : "与上年持平";
  const scope = group.changed_component_count > 0
    ? `，其中 ${group.changed_component_count} 个构件条数发生变化`
    : "";
  return `${previous}；${latest}。${trend}${scope}。`;
}

/**
 * 最新年度与上一年度的病害对比，按构件类型成文。
 *
 * 替换掉了原来的"年度检测进展"步骤条：那个步骤条读的是有没有正式结论，结论一出就
 * 永远停在"已完成"，每次打开都在重复同一句话。
 *
 * 口径是**条数**不是病害身份：未整理的观测没有跨年线索，`defect_comparisons` 又是
 * 模块 07 的预留面、当前为空。所以行文只说"多了/少了多少条"，不写"新增了 N 处病害"，
 * 也不做"高度集中于某部位"这类判断——那是人写报告时的结论，不是数据本身。
 */
function DefectComparisonCard({ comparison }: DefectComparisonCardProps) {
  if (!comparison.available) {
    return (
      <Card size="small" title="年度病害对比" style={{ height: "100%" }}>
        <Typography.Text type="secondary">需要两个已确认的年度检测才能对比，当前不足两个。</Typography.Text>
      </Card>
    );
  }

  // 汇总句用 JS 拼而不是写在 JSX 里：JSX 的换行会在中文标点后留下空格（"，  净增"）。
  const net = comparison.latest_observation_count - comparison.previous_observation_count;
  const summary = `合计：${comparison.previous_year} 年记录病害 `
    + `${comparison.previous_observation_count} 条，${comparison.latest_year} 年 `
    + `${comparison.latest_observation_count} 条。`
    + `${comparison.changed_component_count} 个构件条数发生变化，`
    + `其中增加 ${comparison.increased_observation_count} 条、`
    + `减少 ${comparison.decreased_observation_count} 条，`
    + `净${net >= 0 ? "增" : "减"} ${Math.abs(net)} 条；`
    + `${comparison.unchanged_component_count} 个构件与上年持平。`;
  return (
    <Card
      size="small"
      title="年度病害对比"
      extra={<Typography.Text type="secondary">{comparison.previous_year} 年 → {comparison.latest_year} 年</Typography.Text>}
      style={{ height: "100%" }}
    >
      {/* 89 个构件逐条成文会把概览页撑爆，限高滚动。高度跟着视口走：屏幕高就多给几行，
          屏幕矮就自己收，不把整页顶出竖滚动条。 */}
      <div style={{ maxHeight: OVERVIEW_SCROLL_HEIGHT, overflowY: "auto" }}>
        {comparison.groups.map((group) => (
          <Typography.Paragraph key={`${group.structure_part}-${group.component_type}`}>
            <strong>{group.structure_part}·{group.component_type}</strong>
            <Typography.Text type="secondary">（{group.component_count} 个构件）</Typography.Text>
            ：{describeGroup(group, comparison)}
          </Typography.Paragraph>
        ))}
      </div>

      <Divider size="small" />
      <Typography.Paragraph strong>{summary}</Typography.Paragraph>
      <Typography.Text type="secondary">以上按病害条数统计，不代表逐条病害的对应关系。</Typography.Text>
    </Card>
  );
}

export function BridgeOverviewPage() {
  const { overview } = useBridgeWorkspace();
  const { user } = useAuth();
  const { bridge, latest_inspection: latest } = overview;
  const { profile, error: profileError, setProfile } = useBridgeProfile(bridge.id);
  const { media, replace: replaceMedia, remove: removeMedia } = useBridgeMedia(bridge.id);
  const isAdmin = user?.role === "admin";
  const navigate = useNavigate();
  const { token } = theme.useToken();
  const stacked = Grid.useBreakpoint().xl === false;

  // 四个档案环节齐了就是 100%。这个比例只在上面的指标行里出一次；
  // 原来还有一张把它展开写一遍的卡片，那块位置现在给了地理位置。
  const completeness = [
    Boolean(bridge.system_number && bridge.bridge_name),
    latest !== null,
    overview.defect_archive.component_count > 0,
    overview.pending.total_count === 0,
  ];
  const completenessPercent = Math.round(
    completeness.filter(Boolean).length / completeness.length * 100,
  );

  const metrics: Array<{ label: string; value: string | number; tone: MetricTone; icon: JSX.Element }> = [
    { label: "档案完整度", value: `${completenessPercent}%`, tone: "primary", icon: <FileDoneOutlined /> },
    { label: "待处理资料", value: overview.pending.import_count, tone: "warning", icon: <AuditOutlined /> },
    { label: "病害构件", value: overview.defect_archive.component_count, tone: "success", icon: <ApartmentOutlined /> },
    { label: "跨年病害线索", value: overview.defect_archive.thread_count, tone: "neutral", icon: <HistoryOutlined /> },
  ];

  return (
    <Flex vertical gap={10}>
      <Row gutter={[12, 12]} role="group" aria-label="桥梁档案概况">
        {metrics.map((metric) => (
          <Col key={metric.label} xs={24} sm={12} xl={6}>
            <MetricCard size="small" title={metric.label} value={metric.value} tone={metric.tone} icon={metric.icon} />
          </Col>
        ))}
      </Row>

      <Row gutter={[10, 10]}>
        <Col xs={24} xl={13}>
          {/* 同一行右边是桥梁概况，录满时比这张卡片高，结论居中放在多出来的高度里。 */}
          <Card
            size="small"
            title="最新技术状况"
            style={{ height: "100%" }}
            styles={{
              root: { display: "flex", flexDirection: "column" },
              body: { flex: 1, display: "flex", alignItems: "center", justifyContent: "center" },
            }}
          >
            <Flex vertical align="center" gap={6} aria-live="polite">
              <FileDoneOutlined style={{ fontSize: 40, color: token.colorTextQuaternary }} aria-hidden="true" />
              <Typography.Title level={4} style={{ margin: 0 }}>
                {latest ? `${latest.inspection_year} 年度综合评定` : "尚无正式年度结论"}
              </Typography.Title>
              {latest ? (
                <Typography.Text type="secondary">
                  综合评分 <Typography.Text strong>{show(latest.overall_score)}</Typography.Text>
                  {" · "}综合评定 <Typography.Text strong>{show(latest.overall_grade)}</Typography.Text>
                </Typography.Text>
              ) : (
                <Typography.Text type="secondary">完成年度检测并确认后，将在此生成综合评定结论。</Typography.Text>
              )}
              <Button type="primary" onClick={() => navigate(inspectionsPath(bridge.id))}>进入年度检测</Button>
            </Flex>
          </Card>
        </Col>

        <Col xs={24} xl={11}>
          {/* 这一行的高度只由左边的最新技术状况决定：桥梁概况铺满同样高的格子，
              多出来的项在卡片里滚动，不把这一行撑高。上下叠放时给它一个固定高度。 */}
          <div style={stacked ? { height: 360 } : { position: "relative", height: "100%" }}>
            <div style={stacked ? { height: "100%" } : { position: "absolute", inset: 0 }}>
              <BridgeProfileCard
                profile={profile}
                error={profileError}
                canEdit={isAdmin}
                onSaved={setProfile}
                media={media}
                onMediaReplaced={replaceMedia}
                onMediaRemoved={removeMedia}
              />
            </div>
          </div>
        </Col>

        <Col xs={24} xl={13}>
          <DefectComparisonCard comparison={overview.defect_comparison} />
        </Col>

        <Col xs={24} xl={11}>
          <BridgeLocationCard
            profile={profile}
            locationMap={media.find((item) => item.slot === "LOCATION_MAP")}
          />
        </Col>
      </Row>
    </Flex>
  );
}
