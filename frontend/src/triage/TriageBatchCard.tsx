import { Button, Card, Flex, Tag, Typography } from "antd";

import type { TriageBatchSummary, TriageSampleGroup } from "../api/threadTriageApi";

/**
 * 批次卡片：**纯展示**。
 *
 * 它不自行请求、不自行写库——旧整理页的每张卡各发一次候选请求，1197 张卡就把浏览器
 * 连接池堵死了。展开、剔除、提交、刷新一律由页面统一管。
 */
interface TriageBatchCardProps {
  batch: TriageBatchSummary;
  expanded: boolean;
  busy: boolean;
  onToggleExpand: () => void;
  onConfirm: () => void;
  onSkip: () => void;
  children?: React.ReactNode;
}

function yearSpan(years: number[]): string {
  if (years.length === 0) return "无年度";
  if (years.length === 1) return `仅 ${years[0]}`;
  return years.join(" → ");
}

function sampleLabel(group: TriageSampleGroup): string {
  // 构件业务编号是人唯一能据以分辨的东西。缺了就退回 group_id 前 8 位——总好过
  // 三张卡片都写着"盖梁"，那正是旧页面从根上没法用的原因。
  return group.business_component_code ?? `构件 ${group.group_id.slice(0, 8)}`;
}

export function TriageBatchCard({
  batch, expanded, busy, onToggleExpand, onConfirm, onSkip, children,
}: TriageBatchCardProps) {
  const location = batch.defect_location ?? "（无位置）";
  return (
    <Card
      role="region"
      aria-label={`批次 ${batch.component_type} ${batch.defect_type}`}
      title={
        <Flex vertical gap={2}>
          <Typography.Text strong>
            {batch.structure_part}｜{batch.component_type} · {batch.defect_type} · {location}
          </Typography.Text>
          <Typography.Text type="secondary" style={{ fontWeight: "normal" }}>{yearSpan(batch.year_set)}</Typography.Text>
        </Flex>
      }
      extra={
        <Tag color={batch.action === "create" ? "blue" : "green"} variant="filled">
          {batch.action === "create" ? "批量新建" : "批量绑定"}
        </Tag>
      }
    >
      <Flex vertical gap={12}>
        <Flex align="center" gap={16} wrap>
          <Typography.Text>{batch.group_count} 个构件 · {batch.observation_count} 条观测</Typography.Text>
          {batch.action === "bind" ? (
            <Typography.Text type="secondary">将分别绑定到各构件中精确命中的已有线索</Typography.Text>
          ) : null}
        </Flex>

        <Flex gap={8} wrap>
          {batch.sample_groups.map((group) => (
            <Tag key={group.group_id} variant="filled">
              {sampleLabel(group)}
              <Typography.Text type="secondary"> {group.years.join(" · ")}</Typography.Text>
            </Tag>
          ))}
          {batch.group_count > batch.sample_groups.length ? (
            <Typography.Text type="secondary">…还有 {batch.group_count - batch.sample_groups.length} 个</Typography.Text>
          ) : null}
        </Flex>

        {expanded ? children : null}

        <Flex gap={8} wrap>
          <Button type="primary" disabled={busy} aria-label={`确认这 ${batch.group_count} 组`} onClick={onConfirm}>
            确认这 {batch.group_count} 组
          </Button>
          <Button disabled={busy} aria-label="展开逐组核对" onClick={onToggleExpand}>
            {expanded ? "收起" : "展开逐组核对"}
          </Button>
          <Button disabled={busy} aria-label="暂不处理" onClick={onSkip}>暂不处理</Button>
        </Flex>
      </Flex>
    </Card>
  );
}
