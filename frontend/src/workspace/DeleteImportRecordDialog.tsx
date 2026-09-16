import { Alert, Button, Descriptions, Flex, Form, Input, Modal, Typography } from "antd";
import { useEffect, useState } from "react";

import { ApiError } from "../api/apiClient";
import {
  deleteImportRecord,
  fetchImportRecordDeletionImpact,
  type DeleteImportRecordResult,
  type ImportRecordDeletionImpact,
  workspaceErrorMessage,
} from "../api/workspaceApi";
import { backendBaseUrl } from "../config";

interface Props {
  importRecordId: string;
  onClose: () => void;
  onDeleted: (result: DeleteImportRecordResult) => void;
}

function blockMessage(impact: ImportRecordDeletionImpact): string | null {
  if (impact.block_code === "import_record_edit_locked") return "当前有人正在编辑，必须等编辑者退出或编辑锁过期后才能删除。";
  if (impact.block_code === "import_record_has_formal_facts") return "该记录已经形成正式病害、照片或评分事实，不能单独删除。请使用年度删除能力。";
  if (impact.block_code === "import_record_not_deletable") return "该记录已进入正式只读状态，不能单独删除。请使用年度删除能力。";
  return impact.can_delete ? null : "当前导入记录不能删除。";
}

export function DeleteImportRecordDialog({ importRecordId, onClose, onDeleted }: Props) {
  const [impact, setImpact] = useState<ImportRecordDeletionImpact | null>(null);
  const [reason, setReason] = useState("");
  const [confirmation, setConfirmation] = useState("");
  const [loading, setLoading] = useState(true);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const loadImpact = () => {
    setLoading(true);
    setError(null);
    fetchImportRecordDeletionImpact(backendBaseUrl, importRecordId)
      .then(setImpact)
      .catch((caught) => setError(workspaceErrorMessage(caught)))
      .finally(() => setLoading(false));
  };

  useEffect(loadImpact, [importRecordId]);

  async function submit() {
    if (!impact) return;
    setSubmitting(true);
    setError(null);
    try {
      const result = await deleteImportRecord(backendBaseUrl, importRecordId, {
        impact_token: impact.impact_token,
        confirmation_text: confirmation,
        reason: reason.trim(),
      });
      onDeleted(result);
    } catch (caught) {
      setError(workspaceErrorMessage(caught));
      if (caught instanceof ApiError && (
        caught.code === "deletion_impact_changed" || caught.code === "import_record_edit_locked"
        || caught.code === "import_record_has_formal_facts" || caught.code === "import_record_not_deletable"
      )) {
        setConfirmation("");
        try {
          setImpact(await fetchImportRecordDeletionImpact(backendBaseUrl, importRecordId));
        } catch {
          // 保留原冲突提示；用户也可以关闭弹窗后重新打开。
        }
      }
    } finally {
      setSubmitting(false);
    }
  }

  const blocked = impact ? blockMessage(impact) : null;
  const canDelete = impact?.can_delete === true && reason.trim().length > 0
    && confirmation === impact.confirmation_text && !submitting;

  return (
    <Modal
      open
      centered
      width={560}
      title="永久删除导入记录"
      mask={{ closable: false }}
      onCancel={onClose}
      footer={[
        <Button key="cancel" onClick={onClose} disabled={submitting}>取消</Button>,
        error && !impact ? <Button key="reload" onClick={loadImpact}>重新加载</Button> : null,
        <Button key="delete" type="primary" danger loading={submitting} disabled={!canDelete} onClick={() => void submit()}>
          永久删除此导入记录
        </Button>,
      ]}
    >
      <Flex vertical gap={12}>
        {loading ? <Typography.Text type="secondary">正在核对删除影响…</Typography.Text> : null}
        {impact ? (
          <>
            <Alert
              type="error"
              role="note"
              title="此操作不可撤销"
              description={`只删除“${impact.import_record.import_name}”（${impact.import_record.system_number}），不会删除 ${impact.inspection_year.year} 年度检测或其他导入记录。`}
            />
            <Typography.Text type="secondary">
              {impact.bridge.bridge_name} · {impact.inspection_year.year} 年 V{impact.inspection_year.version_number} · {impact.import_record.status}
            </Typography.Text>
            <Descriptions
              size="small"
              bordered
              column={2}
              items={[
                { key: "defects", label: "候选病害", children: impact.counts.defects },
                { key: "photos", label: "照片候选", children: impact.counts.photos },
                { key: "archived", label: "归档文件", children: impact.counts.archived_files_to_delete },
                { key: "word", label: "临时 Word", children: impact.counts.temporary_word_files_to_delete },
                { key: "workdirs", label: "解析工作目录", children: impact.counts.parse_work_directories_to_delete },
              ]}
            />
            {impact.counts.shared_files_retained > 0 ? (
              <Typography.Text type="secondary">
                另有 {impact.counts.shared_files_retained} 个共享文件仍被其他资料引用，将保留。
              </Typography.Text>
            ) : null}
            {blocked ? (
              <Alert
                type="warning"
                showIcon
                title="当前不能删除"
                description={
                  <>
                    <div>{blocked}</div>
                    {impact.active_edit_locks.map((lock) => (
                      <div key={lock.import_record_id}>
                        {lock.owner_display_name}（{lock.owner_username}）正在编辑。
                      </div>
                    ))}
                  </>
                }
              />
            ) : null}
            <Form layout="vertical" requiredMark={false}>
              <Form.Item label="删除原因" htmlFor="delete-import-reason">
                <Input.TextArea
                  id="delete-import-reason"
                  value={reason}
                  maxLength={1000}
                  autoSize={{ minRows: 2, maxRows: 5 }}
                  placeholder="例如：重复上传、误选报告"
                  onChange={(event) => setReason(event.target.value)}
                />
              </Form.Item>
              <Form.Item label={`请输入“${impact.confirmation_text}”确认`} htmlFor="delete-import-confirmation">
                <Input
                  id="delete-import-confirmation"
                  value={confirmation}
                  autoComplete="off"
                  onChange={(event) => setConfirmation(event.target.value)}
                />
              </Form.Item>
            </Form>
          </>
        ) : null}
        {error ? <Alert type="error" showIcon title={error} /> : null}
      </Flex>
    </Modal>
  );
}
