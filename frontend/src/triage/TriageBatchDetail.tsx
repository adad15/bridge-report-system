import { Alert, Checkbox, Flex, Table, Typography, theme, type TableColumnsType } from "antd";

import type { TriageApplyIssue, TriageBatchDetail, TriageDetailGroup } from "../api/threadTriageApi";

/**
 * 批次明细：以构件为行、年份为列横向排开。
 *
 * 这是"写病害发展"最需要的视角——一眼看出同一处病害三年怎么变的。明细一次取全不分页：
 * 分页会让"跨页剔除"与"提交清单"对不上，用户在第 3 页去掉两组，提交时前端得凑齐全部
 * 组才能表达"这批除了这两组"。
 */
interface TriageBatchDetailProps {
  detail: TriageBatchDetail;
  excludedGroupIds: ReadonlySet<string>;
  issues: TriageApplyIssue[];
  onToggleGroup: (groupId: string) => void;
}

export function TriageBatchDetailTable({
  detail, excludedGroupIds, issues, onToggleGroup,
}: TriageBatchDetailProps) {
  const { token } = theme.useToken();
  const issuesByGroup = new Map<string, TriageApplyIssue[]>();
  for (const issue of issues) {
    if (!issue.group_id) continue;
    const bucket = issuesByGroup.get(issue.group_id) ?? [];
    bucket.push(issue);
    issuesByGroup.set(issue.group_id, bucket);
  }

  const columns: TableColumnsType<TriageDetailGroup> = [
    {
      title: "纳入",
      key: "included",
      width: 64,
      align: "center",
      render: (_value, group) => (
        <Checkbox
          checked={!excludedGroupIds.has(group.group_id)}
          aria-label={`纳入 ${group.business_component_code ?? group.group_id}`}
          onChange={() => onToggleGroup(group.group_id)}
        />
      ),
    },
    {
      title: "构件",
      key: "component",
      width: 160,
      render: (_value, group) => group.business_component_code ?? group.group_id.slice(0, 8),
    },
    ...detail.year_set.map((year) => ({
      title: String(year),
      key: `year-${year}`,
      render: (_value: unknown, group: TriageDetailGroup) => {
        const observation = group.observations.find((item) => item.inspection_year === year);
        if (!observation) return <Typography.Text type="secondary">—</Typography.Text>;
        const parts = [
          observation.scale ? `标度 ${observation.scale}` : "—",
          observation.measurements && observation.measurements.length > 0
            ? observation.measurements.join("；") : "",
          observation.photos && observation.photos.length > 0 ? `${observation.photos.length} 张` : "",
        ].filter((part) => part.length > 0);
        return parts.join(" · ");
      },
    })),
    // 逐组显示各自的 BHXS：批次层面没有单一目标。
    ...(detail.action === "bind" ? [{
      title: "目标线索",
      key: "target",
      width: 150,
      render: (_value: unknown, group: TriageDetailGroup) => group.target_thread?.system_number ?? "—",
    }] : []),
  ];

  return (
    <Flex vertical gap={12} aria-label="批次逐组明细">
      <Table<TriageDetailGroup>
        rowKey="group_id"
        size="small"
        bordered
        columns={columns}
        dataSource={detail.groups}
        pagination={false}
        scroll={{ x: 560, y: 360 }}
        onRow={(group) => ({
          style: {
            opacity: excludedGroupIds.has(group.group_id) ? 0.55 : undefined,
            background: (issuesByGroup.get(group.group_id) ?? []).length > 0 ? token.colorErrorBg : undefined,
          },
        })}
      />

      {issues.length > 0 ? (
        <Alert
          type="error"
          showIcon
          title="这一批里有处理不了的组"
          description={
            <Flex vertical gap={2}>
              {issues.map((issue, index) => (
                <Typography.Text key={`${issue.reason_code}-${index}`}>
                  {issue.group_id ? `${issue.group_id.slice(0, 8)}：` : ""}{issue.message}
                </Typography.Text>
              ))}
            </Flex>
          }
        />
      ) : null}
    </Flex>
  );
}
