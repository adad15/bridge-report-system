import { Card, Flex, Typography } from "antd";

import type { ArchiveObservation, ArchiveThread } from "../api/componentArchiveApi";
import { categoryColor } from "../review/categoryColor";
import { ObservationTable } from "./ObservationTable";

interface DefectThreadCardProps {
  thread: ArchiveThread;
  componentType: string;
  onRebind?: (observation: ArchiveObservation) => void;
}

// A1 病害线索卡片（模块 06 §7.2）：病害是一级单位，年度观测在卡片内纵向排列。
// 标题展示"标准病害类型｜标准位置"两层位置语义中的线索层；年度实际位置在观测行展开后可见。
export function DefectThreadCard({ thread, componentType, onRebind }: DefectThreadCardProps) {
  return (
    <Card
      size="small"
      // 左边一道构件类别色：同一类别在校对工作台和这里长一个样。
      styles={{ root: { borderLeft: `3px solid ${categoryColor(componentType)}` }, body: { paddingTop: 0 } }}
      title={
        <Flex align="center" gap={12} wrap>
          <Typography.Text strong>{thread.defect_type}</Typography.Text>
          <Typography.Text type="secondary">标准位置：{thread.defect_location || "未记录"}</Typography.Text>
        </Flex>
      }
      extra={
        <Flex align="center" gap={12} wrap>
          <Typography.Text type="secondary">
            {thread.first_seen_year !== null && thread.latest_seen_year !== null
              ? `${thread.first_seen_year} - ${thread.latest_seen_year}`
              : "暂无当前有效观测"}
          </Typography.Text>
          <Typography.Text type="secondary">{thread.system_number}</Typography.Text>
        </Flex>
      }
    >
      {thread.observations.length > 0 ? (
        <ObservationTable observations={thread.observations} onRebind={onRebind} />
      ) : (
        <Typography.Text type="secondary">该线索在当前有效版本中暂无观测记录。</Typography.Text>
      )}
    </Card>
  );
}
