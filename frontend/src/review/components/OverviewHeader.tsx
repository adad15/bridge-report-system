import { Badge, Button, Descriptions, Divider, Flex, Popover, Tag, Typography, theme } from "antd";
import { DownOutlined, UpOutlined } from "@ant-design/icons";
import { useState, type ReactNode } from "react";

import type { ReviewResponse } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import type { ReviewCounts } from "../grouping";

interface OverviewHeaderProps {
  response: ReviewResponse;
  draft: BridgeAnnualInspectionData;
  counts: ReviewCounts;
  statusNotice?: ReactNode;
}

function statusTagColor(importStatus: string): string {
  if (importStatus === "待校对") return "warning";
  if (importStatus === "已确认") return "success";
  return "default";
}

// 工作台页眉（模块 05 §7.1，布局见 2026-07-12 布局设计 §5）：主行只放桥名/年度/编号/
// 状态徽章 + 三个进度徽章，其余字段和顶层解析 warnings/errors 收进"详情"折叠区。
// 计数用 counts（来自实时 draft 的 buildStatistics），不用 response.statistics
// （那是拉取时的快照，编辑后会过期）。
export function OverviewHeader({ response, draft, counts, statusNotice }: OverviewHeaderProps) {
  const { token } = theme.useToken();
  const [detailsOpen, setDetailsOpen] = useState(false);
  const { bridge, inspection_year, import_record } = response;
  // 折叠后解析问题不能被埋掉：详情按钮上挂红点计数提醒用户展开查看。
  const issueCount = draft.errors.length + draft.warnings.length;

  const details = (
    <Flex vertical gap={10} style={{ maxWidth: 560, maxHeight: 360, overflowY: "auto" }}>
      <Descriptions
        size="small"
        column={2}
        styles={{ label: { whiteSpace: "nowrap" } }}
        items={[
          { key: "source", label: "来源类型", children: import_record.source_type },
          { key: "importer", label: "解析规则", children: import_record.importer_name ?? "-" },
          { key: "defects", label: "病害候选数量", children: counts.defect_count },
          { key: "photos", label: "照片候选数量", children: counts.photo_count },
          { key: "ratings", label: "评分项数量", children: counts.rating_item_count },
        ]}
      />
      {draft.errors.map((item, index) => (
        <Typography.Text key={`error-${index}`} type="danger">{item.message}</Typography.Text>
      ))}
      {draft.warnings.map((item, index) => (
        <Typography.Text key={`warning-${index}`} type="warning">{item.message}</Typography.Text>
      ))}
    </Flex>
  );

  return (
    <Flex
      component="header"
      align="center"
      gap={12}
      wrap
      style={{ padding: "10px 16px", borderBottom: `1px solid ${token.colorSplit}` }}
    >
      <Typography.Title level={5} style={{ margin: 0 }}>
        {inspection_year ? `${inspection_year.inspection_year} 年度检测` : "导入资料"} · {import_record.source_type}校对
      </Typography.Title>
      <Typography.Text type="secondary">{bridge.bridge_name} · {import_record.system_number}</Typography.Text>
      <Tag color={statusTagColor(import_record.import_status)} variant="filled">{import_record.import_status}</Tag>

      <Popover
        open={detailsOpen}
        placement="bottomLeft"
        trigger="click"
        destroyOnHidden
        content={details}
        onOpenChange={setDetailsOpen}
      >
        <Badge count={issueCount} size="small">
          <Button type="text" size="small" aria-expanded={detailsOpen}>
            详情 {detailsOpen ? <UpOutlined /> : <DownOutlined />}
          </Button>
        </Badge>
      </Popover>

      {statusNotice}

      <Flex align="center" gap={10} style={{ marginInlineStart: "auto" }} wrap>
        <Typography.Text type="secondary">待确认 <Typography.Text strong type="warning">{counts.pending_count}</Typography.Text></Typography.Text>
        <Divider orientation="vertical" style={{ margin: 0 }} />
        <Typography.Text type="secondary">已确认 <Typography.Text strong type="success">{counts.confirmed_count}</Typography.Text></Typography.Text>
        <Divider orientation="vertical" style={{ margin: 0 }} />
        <Typography.Text type="secondary">已忽略 <Typography.Text strong>{counts.ignored_count}</Typography.Text></Typography.Text>
      </Flex>
    </Flex>
  );
}
