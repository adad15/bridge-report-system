import { Flex, Modal, Typography } from "antd";

interface DefectBatchConfirmDialogProps {
  open: boolean;
  defectCount: number;
  photoCount: number;
  removedCount: number;
  /** 待确认记录的规范病害分布，确认前先让用户看清这一批到底是什么。 */
  defectNameDistribution?: Array<{ name: string; count: number }>;
  onCancel: () => void;
  onConfirm: () => void;
}

export function DefectBatchConfirmDialog({
  open,
  defectCount,
  photoCount,
  removedCount,
  defectNameDistribution = [],
  onCancel,
  onConfirm,
}: DefectBatchConfirmDialogProps) {
  return (
    <Modal
      open={open}
      title="确认安全病害"
      okText="确认所选"
      cancelText="取消"
      okButtonProps={{ disabled: defectCount === 0 }}
      onOk={onConfirm}
      onCancel={onCancel}
    >
      <Flex vertical gap={10}>
        <Typography.Text>将确认 {defectCount} 条病害及 {photoCount} 张唯一高置信照片。</Typography.Text>
        {defectNameDistribution.length > 0 ? (
          <Flex vertical gap={4} role="group" aria-label="规范病害分布">
            {defectNameDistribution.map((item) => (
              <Flex key={item.name} align="baseline" justify="space-between" gap={12}>
                <Typography.Text type="secondary">{item.name}</Typography.Text>
                <Typography.Text strong>{item.count}</Typography.Text>
              </Flex>
            ))}
          </Flex>
        ) : null}
        {removedCount > 0 ? (
          <Typography.Text type="warning">有 {removedCount} 条因条件变化已自动移除，不会被修改。</Typography.Text>
        ) : null}
        <Typography.Text type="secondary">此操作只更新当前校对草稿，不会自动保存或正式入库。</Typography.Text>
      </Flex>
    </Modal>
  );
}
