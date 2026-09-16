import { ArrowRightOutlined, FileTextOutlined } from "@ant-design/icons";
import {
  Alert,
  Button,
  Card,
  Col,
  Descriptions,
  Empty,
  Flex,
  Row,
  Spin,
  Table,
  Tag,
  Typography,
} from "antd";

import type {
  RatingTreeNode,
  RatingTreeNodeSummary,
  RatingTreeVersion,
} from "../api/ratingTreeApi";
import type { StandardCatalog } from "../api/standardsApi";
import { ratingTreeDisplayLabel, ratingTreeSectionNumber } from "../rating-tree/ratingTreeLabels";

interface RatingTreeNodeDetailViewProps {
  version: RatingTreeVersion;
  node: RatingTreeNode | null;
  children: RatingTreeNodeSummary[];
  catalog: StandardCatalog | null;
  loading: boolean;
  childrenLoading: boolean;
  onSelectChild: (node: RatingTreeNodeSummary) => void;
}

function scopeNames(
  values: string[],
  names: Map<string, string>,
): string[] {
  if (values.length === 0) return ["不限"];
  const resolved = values.flatMap((value) => names.get(value) ?? []);
  if (resolved.length === values.length) return resolved;
  if (resolved.length > 0) return [...resolved, `其他 ${values.length - resolved.length} 项`];
  return [`${values.length} 个构件类别`];
}

function nodeTypeLabel(nodeType: string): string {
  const labels: Record<string, string> = {
    root: "规则根节点",
    structure_group: "结构层级",
    component_group: "构件类别",
    defect: "病害节点",
  };
  return labels[nodeType] ?? "评定节点";
}

function scoringLabel(node: RatingTreeNode): string {
  if (!node.is_scoring) return node.is_selectable ? "记录节点" : "目录节点";
  return node.uses_source_scale_descriptions ? "参照 H21 扣分" : "按病害标度扣分";
}

function summarySource(version: RatingTreeVersion, node: RatingTreeNode): string {
  if (node.h21_source_table) return `H21 ${node.h21_source_table}`;
  const source = node.sources[0] ?? version.sources[0];
  const candidate = source?.title ?? source?.reference ?? source?.package_version;
  if (candidate && candidate.length <= 28 && !candidate.includes("/") && !candidate.includes("\\")) return candidate;
  if (version.h21_package_version) return `JTG/T H21 · v${version.h21_package_version}`;
  return "随发布版本固化";
}

function SectionTitle({ children }: { children: string }) {
  return <Typography.Title level={5} style={{ margin: 0 }}>{children}</Typography.Title>;
}

function ScaleRulesPanel({ node }: { node: RatingTreeNode }) {
  return (
    <Card
      size="small"
      role="region"
      aria-label="标度判定与扣分"
      title={<SectionTitle>标度判定与扣分</SectionTitle>}
      extra={<Typography.Text type="secondary">{node.allowed_scales.length > 0 ? `${node.allowed_scales.length} 个标度` : "暂无标度"}</Typography.Text>}
    >
      <Flex vertical gap={10}>
        {node.uses_source_scale_descriptions ? (
          <Typography.Text type="secondary">判定来源：来源软件 {node.display_number || ""}；扣分参照：H21 {node.h21_source_table || "—"}</Typography.Text>
        ) : node.h21_source_table ? <Typography.Text type="secondary">来源：{node.h21_source_table}</Typography.Text> : null}
        {node.allowed_scales.length === 0 ? (
          <Typography.Text type="secondary">当前病害节点暂无标度扣分规则。</Typography.Text>
        ) : (
          <Table
            size="small"
            rowKey="scale"
            pagination={false}
            dataSource={node.allowed_scales.map((scale) => ({
              scale,
              description: node.scale_descriptions[String(scale)] || "—",
              deduction: node.deduction_points[String(scale)] ?? "—",
            }))}
            columns={[
              { title: "标度", dataIndex: "scale", width: 72 },
              { title: "判定说明", dataIndex: "description" },
              { title: "扣分", dataIndex: "deduction", width: 80 },
            ]}
          />
        )}
      </Flex>
    </Card>
  );
}

export function RatingTreeNodeDetailView({
  version,
  node,
  children,
  catalog,
  loading,
  childrenLoading,
  onSelectChild,
}: RatingTreeNodeDetailViewProps) {
  if (loading) {
    return (
      <Flex align="center" justify="center" gap={8} style={{ minHeight: 240 }}>
        <Spin />
        <Typography.Text type="secondary">正在加载节点详情…</Typography.Text>
      </Flex>
    );
  }
  if (node === null) {
    return (
      <Empty
        description={
          <Flex vertical gap={4}>
            <Typography.Text strong>{version.tree_name}</Typography.Text>
            <Typography.Text type="secondary">从左侧选择节点，查看适用范围和评分规则。</Typography.Text>
          </Flex>
        }
      />
    );
  }

  const bridgeTypeNames = new Map((catalog?.bridge_types ?? []).map((item) => [item.id, item.name]));
  const componentNames = new Map((catalog?.component_categories ?? []).map((item) => [item.id, item.name]));
  const bridgeScopes = scopeNames(node.bridge_type_ids, bridgeTypeNames);
  const componentScopes = scopeNames(node.component_category_ids, componentNames);
  const sectionNumber = ratingTreeSectionNumber(node) ?? "—";
  const childHeading = children.some((child) => child.node_type === "defect") ? "下级病害节点" : "下级评定项目";
  const knownScales = node.allowed_scales.length > 0
    ? node.allowed_scales
    : [...new Set(children.flatMap((child) => child.allowed_scales ?? []))].sort((a, b) => a - b);
  const deductions = Object.values(node.deduction_points).filter((value): value is number => typeof value === "number");
  const maximumDeduction = deductions.length > 0 ? Math.max(...deductions) : null;

  return (
    <Flex component="article" vertical gap={16}>
      <Flex vertical gap={4}>
        <Typography.Text type="secondary">
          {node.path.filter((item) => item.node_type !== "root").map((item) => ratingTreeDisplayLabel(item)).join(" / ") || version.tree_name}
        </Typography.Text>
        <Flex align="center" gap={10} wrap>
          <Typography.Title level={3} style={{ margin: 0 }}>{ratingTreeDisplayLabel(node)}</Typography.Title>
          <Tag color="blue">{nodeTypeLabel(node.node_type)}</Tag>
          <Tag color={node.is_scoring ? "green" : node.is_selectable ? "gold" : "default"}>
            {node.is_scoring ? "计分节点" : node.is_selectable ? "记录节点" : "目录节点"}
          </Tag>
        </Flex>
      </Flex>

      <Descriptions
        bordered
        size="small"
        column={{ xs: 1, md: 2, xl: 4 }}
        aria-label="节点元数据"
        items={[
          { key: "number", label: "节点编号", children: sectionNumber },
          { key: "type", label: "节点类型", children: nodeTypeLabel(node.node_type) },
          { key: "bridge", label: "适用桥型", children: bridgeScopes.join("、") },
          { key: "scoring", label: "计分方式", children: scoringLabel(node) },
        ]}
      />

      <Flex align="center" gap={8} wrap role="group" aria-label="适用范围">
        <Typography.Text type="secondary">适用范围</Typography.Text>
        {componentScopes.map((scope) => <Tag key={scope} color="blue">{scope}</Tag>)}
      </Flex>

      <Row gutter={[16, 16]}>
        <Col xs={24} xl={16}>
          {node.node_type === "defect" ? <ScaleRulesPanel node={node} /> : (
            <Card
              size="small"
              role="region"
              aria-label={childHeading}
              title={<SectionTitle>{childHeading}</SectionTitle>}
              extra={<Typography.Text type="secondary">{childrenLoading ? "正在加载…" : `${children.length} 项`}</Typography.Text>}
            >
              {!childrenLoading && children.length === 0 ? (
                <Typography.Text type="secondary">该节点没有下级项目。</Typography.Text>
              ) : (
                <Flex vertical gap={2}>
                  {children.map((child) => (
                    <Button
                      key={child.id}
                      type="text"
                      block
                      aria-label={ratingTreeDisplayLabel(child)}
                      onClick={() => onSelectChild(child)}
                      style={{ height: "auto", padding: "8px 10px" }}
                    >
                      <Flex align="center" gap={10} style={{ width: "100%" }}>
                        <FileTextOutlined aria-hidden="true" />
                        <Typography.Text ellipsis style={{ flex: 1, textAlign: "left" }}>{child.display_name}</Typography.Text>
                        <Typography.Text type="secondary">{ratingTreeSectionNumber(child) ?? "—"}</Typography.Text>
                        {child.is_scoring ? <Tag color="green">计分节点</Tag> : null}
                        <ArrowRightOutlined aria-hidden="true" />
                      </Flex>
                    </Button>
                  ))}
                </Flex>
              )}
            </Card>
          )}
        </Col>

        <Col xs={24} xl={8}>
          <Flex vertical gap={12}>
            <Card size="small" title={<SectionTitle>评分规则摘要</SectionTitle>}>
              <Descriptions
                size="small"
                column={1}
                items={[
                  { key: "scales", label: "标度", children: knownScales.length > 0 ? `${knownScales[0]}–${knownScales[knownScales.length - 1]}` : "—" },
                  { key: "max", label: "最高扣分", children: maximumDeduction ?? "—" },
                  { key: "source", label: "规则来源", children: summarySource(version, node) },
                ]}
              />
            </Card>
            <Alert type="info" showIcon role="note" title="该版本已发布，仅供查看，不能在此页面修改规则。" />
          </Flex>
        </Col>
      </Row>
    </Flex>
  );
}
