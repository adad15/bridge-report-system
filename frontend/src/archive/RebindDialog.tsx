import { Alert, Button, Checkbox, Flex, Form, Modal, Select, Typography } from "antd";
import { useState } from "react";

import type { ArchiveObservation, ArchiveThread } from "../api/componentArchiveApi";
import { bindObservationThread, threadBindingErrorMessage } from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { backendBaseUrl } from "../config";

interface RebindDialogProps {
  observation: ArchiveObservation;
  /** 该构件的全部线索（来自已加载的档案），跨构件线索不会出现在选项里。 */
  threads: Array<Omit<ArchiveThread, "observations">>;
  onClose: () => void;
  onSuccess: () => void;
}

// 重新绑定/解绑对话框（模块 06 §9 步骤 5）：改变既有绑定必须显式勾选确认；
// 请求携带 updated_at 乐观令牌，过期即被后端 409 拒绝并提示刷新。
export function RebindDialog({ observation, threads, onClose, onSuccess }: RebindDialogProps) {
  const [targetThreadId, setTargetThreadId] = useState<string>(observation.defect_thread_id ?? "");
  const [confirmed, setConfirmed] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function submit(): Promise<void> {
    setBusy(true);
    setError(null);
    try {
      await bindObservationThread(backendBaseUrl, observation.id, {
        defect_thread_id: targetThreadId === "" ? null : targetThreadId,
        expected_observation_updated_at: observation.updated_at,
        confirm_rebind: confirmed,
      });
      onSuccess();
    } catch (caught) {
      if (caught instanceof ApiError) {
        setError(threadBindingErrorMessage(caught.code, caught.message));
      } else {
        setError("重新绑定失败，请稍后重试。");
      }
    } finally {
      setBusy(false);
    }
  }

  const changingExistingBinding =
    observation.defect_thread_id !== null && (targetThreadId === "" ? null : targetThreadId) !== observation.defect_thread_id;

  return (
    <Modal
      open
      centered
      title="重新绑定病害线索"
      onCancel={onClose}
      footer={[
        <Button key="cancel" disabled={busy} onClick={onClose}>取消</Button>,
        <Button
          key="submit"
          type="primary"
          loading={busy}
          disabled={changingExistingBinding && !confirmed}
          onClick={() => void submit()}
        >
          确认绑定
        </Button>,
      ]}
    >
      <Flex vertical gap={12}>
        <Typography.Text type="secondary">
          {observation.inspection_year} 年｜{observation.defect_type}｜{observation.defect_location || "未记录"}
        </Typography.Text>
        <Form layout="vertical">
          <Form.Item label="目标线索" htmlFor="rebind-target-thread">
            <Select
              id="rebind-target-thread"
              value={targetThreadId}
              onChange={setTargetThreadId}
              options={[
                { value: "", label: "（解绑，保持未绑定）" },
                ...threads.map((thread) => ({
                  value: thread.id,
                  label: `${thread.defect_type}｜${thread.defect_location || "未记录"}`,
                })),
              ]}
            />
          </Form.Item>
          {changingExistingBinding ? (
            <Checkbox checked={confirmed} onChange={(event) => setConfirmed(event.target.checked)}>
              我确认改变该观测的既有线索绑定
            </Checkbox>
          ) : null}
        </Form>
        {error ? <Alert type="error" showIcon title={error} /> : null}
      </Flex>
    </Modal>
  );
}
