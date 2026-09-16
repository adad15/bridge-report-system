import { Alert, Button, Descriptions, Flex, Form, Input, Modal, Typography } from "antd";
import { useEffect, useState } from "react";

import { ApiError } from "../api/apiClient";
import {
  deleteInspectionYear,
  fetchInspectionYearDeletionImpact,
  type DeleteInspectionYearResult,
  type InspectionYearDeletionImpact,
  workspaceErrorMessage,
} from "../api/workspaceApi";
import { backendBaseUrl } from "../config";

interface Props {
  inspectionYearId: string;
  onClose: () => void;
  onDeleted: (result: DeleteInspectionYearResult) => void;
}

export function DeleteInspectionYearDialog({ inspectionYearId, onClose, onDeleted }: Props) {
  const [impact, setImpact] = useState<InspectionYearDeletionImpact | null>(null);
  const [reason, setReason] = useState("");
  const [confirmation, setConfirmation] = useState("");
  const [loading, setLoading] = useState(true);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const loadImpact = () => {
    setLoading(true);
    setError(null);
    fetchInspectionYearDeletionImpact(backendBaseUrl, inspectionYearId)
      .then(setImpact)
      .catch((caught) => setError(workspaceErrorMessage(caught)))
      .finally(() => setLoading(false));
  };

  useEffect(loadImpact, [inspectionYearId]);

  async function submit() {
    if (!impact) return;
    setSubmitting(true);
    setError(null);
    try {
      const result = await deleteInspectionYear(backendBaseUrl, inspectionYearId, {
        impact_token: impact.impact_token,
        confirmation_text: confirmation,
        reason: reason.trim(),
      });
      onDeleted(result);
    } catch (caught) {
      setError(workspaceErrorMessage(caught));
      if (caught instanceof ApiError
        && (caught.code === "deletion_impact_changed" || caught.code === "inspection_year_edit_locked")) {
        setConfirmation("");
        try {
          setImpact(await fetchInspectionYearDeletionImpact(backendBaseUrl, inspectionYearId));
        } catch {
          // 保留原始冲突提示；关闭并重新打开弹窗仍可再次加载。
        }
      }
    } finally {
      setSubmitting(false);
    }
  }

  const locked = (impact?.active_edit_locks.length ?? 0) > 0;
  const canDelete = impact !== null && !locked && reason.trim().length > 0
    && confirmation === impact.confirmation_text && !submitting;

  return (
    <Modal
      open
      centered
      width={560}
      title="永久删除年度检测"
      mask={{ closable: false }}
      onCancel={onClose}
      footer={[
        <Button key="cancel" onClick={onClose} disabled={submitting}>取消</Button>,
        error && !impact ? <Button key="reload" onClick={loadImpact}>重新加载</Button> : null,
        <Button key="delete" type="primary" danger loading={submitting} disabled={!canDelete} onClick={() => void submit()}>
          永久删除
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
              description={`将永久删除“${impact.bridge.bridge_name}”${impact.inspection_year} 年的全部版本（${impact.version_numbers.map((item) => `V${item}`).join("、")}），不是只删除当前版本。`}
            />
            <Descriptions
              size="small"
              bordered
              column={2}
              items={[
                { key: "versions", label: "年度版本", children: impact.counts.inspection_versions },
                { key: "imports", label: "导入记录", children: impact.counts.import_records },
                { key: "observations", label: "病害观测", children: impact.counts.defect_observations },
                { key: "photos", label: "病害照片", children: impact.counts.defect_photos },
                { key: "ratings", label: "评分记录", children: impact.counts.condition_ratings },
                { key: "archived", label: "正式归档文件", children: impact.counts.archived_files_to_delete },
                { key: "temporary", label: "临时来源文件", children: impact.counts.temporary_source_files_to_delete },
              ]}
            />
            {impact.counts.shared_files_retained > 0 ? (
              <Typography.Text type="secondary">
                另有 {impact.counts.shared_files_retained} 个共享文件仍被其他资料引用，将保留。
              </Typography.Text>
            ) : null}
            {locked ? (
              <Alert
                type="warning"
                showIcon
                title="当前不能删除"
                description={impact.active_edit_locks.map((lock) => (
                  <div key={lock.import_record_id}>
                    {lock.owner_display_name}（{lock.owner_username}）正在编辑该年度的导入记录。
                  </div>
                ))}
              />
            ) : null}
            <Form layout="vertical" requiredMark={false}>
              <Form.Item label="删除原因" htmlFor="delete-year-reason">
                <Input.TextArea
                  id="delete-year-reason"
                  value={reason}
                  maxLength={1000}
                  autoSize={{ minRows: 2, maxRows: 5 }}
                  placeholder="例如：误建年度、测试数据需要清除"
                  onChange={(event) => setReason(event.target.value)}
                />
              </Form.Item>
              <Form.Item label={`请输入“${impact.confirmation_text}”确认`} htmlFor="delete-year-confirmation">
                <Input
                  id="delete-year-confirmation"
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
