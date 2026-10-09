import { Alert, Flex, Modal, Typography } from "antd";
import { useState } from "react";

import { type StandardDocument, deleteStandardDocument, standardDocumentsError } from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";

interface Props {
  document: StandardDocument;
  onClose: () => void;
  onDeleted: () => void;
}

/** 删除整部规范：全部版本和原文文件一并删除，先确认。 */
export function DeleteStandardDocumentModal({ document, onClose, onDeleted }: Props) {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function confirm() {
    setBusy(true);
    setError(null);
    try {
      await deleteStandardDocument(backendBaseUrl, document.id);
      onDeleted();
    } catch (caught) {
      setError(standardDocumentsError(caught));
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      centered
      width={480}
      title="删除规范"
      mask={{ closable: false }}
      closable={{ "aria-label": "关闭", disabled: busy }}
      onCancel={() => { if (!busy) onClose(); }}
      okText="删除"
      okButtonProps={{ danger: true, loading: busy }}
      cancelText="取消"
      cancelButtonProps={{ disabled: busy }}
      onOk={() => void confirm()}
      destroyOnHidden
    >
      <Flex vertical gap={12}>
        <Typography.Text>
          确定删除「{document.name}」吗？全部 {document.versions.length} 个版本和原文文件会一并删除，无法恢复。
        </Typography.Text>
        {error ? <Alert type="error" showIcon title={error} /> : null}
      </Flex>
    </Modal>
  );
}
