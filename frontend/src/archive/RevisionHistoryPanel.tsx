import { Card, Empty, Flex, Tag, Typography } from "antd";

import type { RevisionGroup } from "../api/componentArchiveApi";
import { StatusTag } from "../workspace/StatusTag";
import { ObservationTable } from "./ObservationTable";

// 历史修订入口（模块 06 §7.4）：旧修订版按 年份+版本号 分组独立只读展示，
// 明确标注被当前版本替代的关系；不提供任何绑定操作，也不计入主档案统计。
export function RevisionHistoryPanel({ revisions }: { revisions: RevisionGroup[] }) {
  if (revisions.length === 0) {
    return <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="该构件没有历史修订版观测" />;
  }

  return (
    <Flex vertical gap={14}>
      {revisions.map((group) => (
        <Card
          key={`${group.inspection_year}-v${group.version_number}`}
          size="small"
          styles={{ body: { paddingTop: 0 } }}
          title={
            <Flex align="center" gap={10} wrap>
              <Typography.Text strong>{group.inspection_year} 年 v{group.version_number}</Typography.Text>
              <StatusTag status={group.inspection_status} />
            </Flex>
          }
          extra={group.superseded_by_version !== null ? (
            <Tag color="warning" variant="filled">已被修订，当前有效版本为 v{group.superseded_by_version}</Tag>
          ) : null}
        >
          <ObservationTable observations={group.observations} />
        </Card>
      ))}
    </Flex>
  );
}
