import { Flex, Modal, Typography } from "antd";

import type { DefectIssueGroup } from "../defectIssueGroups";

interface DefectIssueGroupConfirmDialogProps {
  group: DefectIssueGroup | null;
  onCancel: () => void;
  onConfirm: () => void;
}

export function DefectIssueGroupConfirmDialog({
  group,
  onCancel,
  onConfirm,
}: DefectIssueGroupConfirmDialogProps) {
  const confirmableCount = group?.rangeSplitConfirmableRows.length ?? 0;
  const excludedCount = group ? group.rows.length - confirmableCount : 0;

  return (
    <Modal
      open={group !== null}
      title="确认本组病害"
      okText={`确认 ${confirmableCount} 条`}
      cancelText="取消"
      okButtonProps={{ disabled: confirmableCount === 0 }}
      onOk={onConfirm}
      onCancel={onCancel}
      destroyOnHidden
    >
      <Flex vertical gap={10}>
        <Typography.Text>“{group?.title}”中有 {confirmableCount} 条病害可以确认。</Typography.Text>
        <Typography.Text type="secondary">无照片记录也会被确认；现有病害选择和照片关联保持不变。</Typography.Text>
        {excludedCount > 0 ? (
          <Typography.Text type="warning">另有 {excludedCount} 条存在其他待处理问题，本次不会确认。</Typography.Text>
        ) : null}
        <Typography.Text type="secondary">本次只完成构件范围拆分核对，修改仍需保存草稿后才会提交。</Typography.Text>
      </Flex>
    </Modal>
  );
}
