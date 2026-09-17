import { Button, Card, Empty, Flex, Select, Typography } from "antd";
import { useMemo, useState } from "react";

import type { RatingTreeNodeSummary } from "../../api/ratingTreeApi";
import { ratingTreeOptionLabel } from "../../rating-tree/ratingTreeLabels";
import { applicableRatingTreeNodes } from "../applicableRatingTreeNodes";
import type { DefectIssueGroup } from "../defectIssueGroups";

interface DefectIssueGroupListProps {
  groups: DefectIssueGroup[];
  nodesByComponent: ReadonlyMap<string, RatingTreeNodeSummary[]>;
  disabled?: boolean;
  onApplyNode: (group: DefectIssueGroup, node: RatingTreeNodeSummary) => void;
  onConfirmGroup: (group: DefectIssueGroup) => void;
  onOpenDefect: (candidateId: string) => void;
}

function commonNodes(
  group: DefectIssueGroup,
  nodesByComponent: ReadonlyMap<string, RatingTreeNodeSummary[]>,
): RatingTreeNodeSummary[] {
  // 只要有一行还没绑构件，这一组就没有"共同适用"可言：整组套用一个节点会把它也写上。
  // 逐行取全部实例构件而不是单一 bridgeComponentId——区间展开的行单一 id 为 null，
  // 按它过滤会让整组的候选凭空清空。
  if (group.rows.some((row) => row.resolution.componentIds.length === 0)) return [];
  return applicableRatingTreeNodes(
    group.rows.flatMap((row) => row.resolution.componentIds),
    nodesByComponent,
  );
}

export function DefectIssueGroupList({
  groups,
  nodesByComponent,
  disabled = false,
  onApplyNode,
  onConfirmGroup,
  onOpenDefect,
}: DefectIssueGroupListProps) {
  const [selectedNodes, setSelectedNodes] = useState<Map<string, string>>(new Map());
  const nodeOptions = useMemo(
    () => new Map(groups.map((group) => [group.key, commonNodes(group, nodesByComponent)])),
    [groups, nodesByComponent],
  );

  if (groups.length === 0) {
    return <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="当前筛选下没有可分组的待处理病害。" />;
  }

  return (
    <Flex vertical gap={12}>
      <Flex align="baseline" gap={10} wrap>
        <Typography.Text strong>{groups.length} 个问题组</Typography.Text>
        <Typography.Text type="secondary">
          共 {groups.reduce((total, group) => total + group.rows.length, 0)} 条待处理病害
        </Typography.Text>
      </Flex>
      {groups.map((group) => {
        const options = nodeOptions.get(group.key) ?? [];
        const selectedNodeId = selectedNodes.get(group.key) ?? "";
        const selectedNode = options.find((node) => node.id === selectedNodeId);
        const canAssign =
          group.kind === "unmatched" &&
          group.hasExactSourceIdentity &&
          group.componentCategoryId !== null &&
          options.length > 0;
        const confirmableCount = group.rangeSplitConfirmableRows.length;
        const excludedCount = group.rows.length - confirmableCount;
        const componentNumbers = [...new Set(group.rows
          .map((row) => row.defect.component_number ?? row.defect.component_name))];
        return (
          <Card
            key={group.key}
            size="small"
            title={
              <Flex vertical gap={2}>
                <Typography.Text type="secondary" style={{ fontWeight: "normal" }}>
                  {group.kind === "unmatched" ? "无匹配结果" : "待处理问题"}
                </Typography.Text>
                <Typography.Text strong>{group.title}</Typography.Text>
              </Flex>
            }
            extra={<Typography.Text strong>{group.rows.length} 条</Typography.Text>}
          >
            <Flex vertical gap={10}>
              <Flex gap={16} wrap>
                {group.sourceGroupNumber || group.sourceIndicatorNumber ? (
                  <Typography.Text type="secondary">
                    来源编号 {group.sourceGroupNumber ?? "?"} / {group.sourceIndicatorNumber ?? "?"}
                  </Typography.Text>
                ) : null}
                <Typography.Text type="secondary">
                  构件 {componentNumbers.slice(0, 4).join("、")}
                  {componentNumbers.length > 4 ? ` 等 ${componentNumbers.length} 个` : ""}
                </Typography.Text>
              </Flex>

              <Flex vertical gap={2}>
                {group.problemMessages.slice(0, 3).map((message) => (
                  <Typography.Text key={message}>{message}</Typography.Text>
                ))}
              </Flex>

              <Flex gap={8} wrap>
                {group.rows.slice(0, 3).map((row) => (
                  <Button key={row.candidateId} size="small" onClick={() => onOpenDefect(row.candidateId)}>
                    {row.defect.component_number ?? row.defect.component_name}
                    <Typography.Text type="secondary"> {row.defect.defect_location || "未记录位置"}</Typography.Text>
                  </Button>
                ))}
              </Flex>

              {canAssign ? (
                <Flex align="center" gap={8} wrap>
                  <Select
                    aria-label={`为 ${group.title} 选择评定树病害`}
                    style={{ minWidth: 260 }}
                    disabled={disabled}
                    value={selectedNodeId}
                    onChange={(value: string) => setSelectedNodes((current) => {
                      const next = new Map(current);
                      next.set(group.key, value);
                      return next;
                    })}
                    options={[
                      { value: "", label: "请选择评定树病害" },
                      ...options.map((node) => ({
                        value: node.id,
                        label: `${ratingTreeOptionLabel(node, options)}${node.is_scoring ? "" : "（暂不计分）"}`,
                      })),
                    ]}
                  />
                  <Button
                    type="primary"
                    disabled={disabled || !selectedNode}
                    onClick={() => selectedNode && onApplyNode(group, selectedNode)}
                  >
                    应用到本组 {group.rows.length} 条
                  </Button>
                </Flex>
              ) : confirmableCount > 0 ? (
                <Flex align="center" gap={10} wrap>
                  <Typography.Text type="secondary">
                    {excludedCount > 0
                      ? `${confirmableCount} 条可确认，${excludedCount} 条存在其他问题。`
                      : "本组仅需确认构件范围拆分结果。"}
                  </Typography.Text>
                  <Button type="primary" disabled={disabled} onClick={() => onConfirmGroup(group)}>
                    确认本组可确认项（{confirmableCount}）
                  </Button>
                </Flex>
              ) : (
                <Typography.Text type="secondary">
                  {group.kind === "unmatched" && !group.hasExactSourceIdentity
                    ? "来源身份不完整，需逐条核对。"
                    : "本组包含其他待处理项，需打开样例继续处理。"}
                </Typography.Text>
              )}
            </Flex>
          </Card>
        );
      })}
    </Flex>
  );
}
