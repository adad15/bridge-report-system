import { Flex, Modal, Typography } from "antd";

import type { RatingTreeNodeSummary } from "../../api/ratingTreeApi";
import { ratingTreeDisplayLabel } from "../../rating-tree/ratingTreeLabels";
import type { DefectIssueGroup } from "../defectIssueGroups";

interface DefectBatchAssignDialogProps {
  assignment: { group: DefectIssueGroup; node: RatingTreeNodeSummary } | null;
  onCancel: () => void;
  onConfirm: () => void;
}

export function DefectBatchAssignDialog({
  assignment,
  onCancel,
  onConfirm,
}: DefectBatchAssignDialogProps) {
  return (
    <Modal
      open={assignment !== null}
      title="批量指定评定树病害"
      okText="应用到本组"
      cancelText="取消"
      onOk={onConfirm}
      onCancel={onCancel}
      destroyOnHidden
    >
      <Flex vertical gap={10}>
        <Typography.Text>
          将同一来源身份下的 {assignment?.group.rows.length} 条病害统一指定为
          “{assignment ? ratingTreeDisplayLabel(assignment.node) : ""}”。
        </Typography.Text>
        <Typography.Text type="secondary">构件、位置、描述、照片和来源编号均保持原值。</Typography.Text>
        <Typography.Text type="secondary">此操作只更新当前校对草稿，不会修改已发布的评定树版本。</Typography.Text>
      </Flex>
    </Modal>
  );
}
