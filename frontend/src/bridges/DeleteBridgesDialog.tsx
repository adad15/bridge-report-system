import { Alert, Button, Card, Flex, Form, Input, Modal, Progress, Typography } from "antd";
import { useEffect, useMemo, useRef, useState } from "react";

import {
  advanceBridgeCleanup,
  bridgeAdministrationError,
  deleteBridges,
  fetchBridgeDeletionImpact,
  type BridgeCleanupProgress,
  type BridgeDeletionPreview,
  type DeleteBridgesResult,
} from "../api/bridgeAdministrationApi";
import { backendBaseUrl } from "../config";
import { ApiError } from "../api/apiClient";

interface Props {
  bridgeIds: string[];
  onClose: () => void;
  onSelectionChanged: () => void;
  onCompleted: () => void;
}

export function DeleteBridgesDialog({ bridgeIds, onClose, onSelectionChanged, onCompleted }: Props) {
  const [preview, setPreview] = useState<BridgeDeletionPreview | null>(null);
  const [reason, setReason] = useState("");
  const [confirmation, setConfirmation] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<DeleteBridgesResult | null>(null);
  const [progress, setProgress] = useState<Record<string, BridgeCleanupProgress>>({});
  const progressRef = useRef(progress);
  progressRef.current = progress;

  useEffect(() => {
    // 删除完成后不再重新拉取影响预览：此时桥梁已不存在，重拉必然 404，
    // 会在成功结果旁误报“删除影响加载失败”。
    if (result) return;
    fetchBridgeDeletionImpact(backendBaseUrl, bridgeIds)
      .then(setPreview)
      .catch(() => setError("删除影响加载失败。"));
  }, [bridgeIds, result]);

  // 删除成功后轮询推进独占文件清理：每轮对每个未清完的审计调用一次 advance，
  // 后端每轮再领一批，直到清完或某一轮不再有进展（余下的交给后台定时器按退避重试）。
  useEffect(() => {
    if (!result) return;
    const audits = result.results
      .filter((item) => item.status === "deleted" && item.audit_id && (item.total_file_count ?? 0) > 0)
      .map((item) => ({
        auditId: item.audit_id as string,
        total: item.total_file_count ?? 0,
        pending: item.pending_file_count ?? 0,
      }));
    if (audits.length === 0) return;

    setProgress((current) => {
      const next = { ...current };
      for (const audit of audits) {
        if (!next[audit.auditId]) {
          next[audit.auditId] = {
            total: audit.total,
            completed: Math.max(audit.total - audit.pending, 0),
            failed: 0,
            pending: audit.pending,
            done: audit.pending === 0,
          };
        }
      }
      return next;
    });

    let cancelled = false;
    let timer: number | undefined;
    const settled = new Set(audits.filter((audit) => audit.pending === 0).map((audit) => audit.auditId));

    const pump = async () => {
      await Promise.all(
        audits
          .filter((audit) => !settled.has(audit.auditId))
          .map(async (audit) => {
            try {
              const next = await advanceBridgeCleanup(backendBaseUrl, audit.auditId);
              if (cancelled) return;
              const previous = progressRef.current[audit.auditId];
              // 本轮没有新增已清理且仍有待清理 -> 余下的处于退避重试，继续轮询也无用，就地收尾。
              if (next.done || (previous && next.completed <= previous.completed)) {
                settled.add(audit.auditId);
              }
              setProgress((current) => ({ ...current, [audit.auditId]: next }));
            } catch {
              // 单次推进失败不致命：交给后台定时器兜底，下一轮继续尝试。
            }
          })
      );
      if (cancelled) return;
      if (settled.size >= audits.length) return;
      timer = window.setTimeout(() => void pump(), 800);
    };
    void pump();

    return () => {
      cancelled = true;
      if (timer !== undefined) window.clearTimeout(timer);
    };
  }, [result]);

  const canDelete = useMemo(
    () => Boolean(preview && reason.trim() && confirmation === preview.confirmation_text && !busy),
    [preview, reason, confirmation, busy]
  );

  async function submit() {
    if (!preview) return;
    setBusy(true);
    setError(null);
    try {
      setResult(await deleteBridges(backendBaseUrl, {
        confirmation_text: confirmation,
        reason: reason.trim(),
        items: preview.bridges.map((item) => ({
          bridge_id: item.bridge.id,
          impact_token: item.impact_token,
        })),
      }));
      onCompleted();
    } catch (caught) {
      setError(bridgeAdministrationError(caught));
      if (caught instanceof ApiError && caught.code === "bridge_selection_changed") {
        onSelectionChanged();
      }
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      centered
      width={760}
      title="永久删除桥梁档案"
      mask={{ closable: false }}
      onCancel={onClose}
      styles={{ body: { maxHeight: "calc(100vh - 220px)", overflowY: "auto", overflowX: "hidden" } }}
      footer={[
        <Button key="close" disabled={busy} onClick={onClose}>{result ? "关闭" : "取消"}</Button>,
        !result ? (
          <Button key="delete" type="primary" danger loading={busy} disabled={!canDelete} onClick={() => void submit()}>
            永久删除
          </Button>
        ) : null,
      ]}
    >
      <Flex vertical gap={12}>
        {!preview && !error && !result ? <Typography.Text type="secondary">正在核对删除影响…</Typography.Text> : null}
        {preview && !result ? (
          <>
            <Alert
              type="error"
              role="note"
              title="此操作不可撤销"
              description="将逐座永久删除所选桥梁的全部年度、导入、病害、评分、独占归档文件和临时来源文件。"
            />
            {preview.bridges.map((item) => (
              <Card key={item.bridge.id} size="small" title={`${item.bridge.system_number}　${item.bridge.bridge_name}`}>
                <Flex vertical gap={8}>
                  <Typography.Text>
                    年度 {item.counts.inspection_years} · 版本 {item.counts.inspection_versions} ·
                    导入 {item.counts.import_records} · 构件 {item.counts.bridge_components} ·
                    病害 {item.counts.defect_observations} · 正式文件 {item.counts.archived_files_to_delete} ·
                    临时文件 {item.counts.temporary_source_files_to_delete}
                  </Typography.Text>
                  {item.active_edit_locks.map((lock) => (
                    <Alert
                      key={lock.import_record_id}
                      type="warning"
                      showIcon
                      title={`${lock.owner_display_name} 正在编辑，本次不能删除`}
                    />
                  ))}
                </Flex>
              </Card>
            ))}
            <Form layout="vertical" requiredMark={false}>
              <Form.Item label="删除原因" htmlFor="delete-bridges-reason">
                <Input.TextArea
                  id="delete-bridges-reason"
                  maxLength={4000}
                  autoSize={{ minRows: 2, maxRows: 5 }}
                  value={reason}
                  onChange={(event) => setReason(event.target.value)}
                />
              </Form.Item>
              <Form.Item label={`请输入“${preview.confirmation_text}”确认`} htmlFor="delete-bridges-confirmation">
                <Input
                  id="delete-bridges-confirmation"
                  autoComplete="off"
                  value={confirmation}
                  onChange={(event) => setConfirmation(event.target.value)}
                />
              </Form.Item>
            </Form>
          </>
        ) : null}
        {result ? (
          <Flex vertical gap={8}>
            <Typography.Title level={5}>已处理</Typography.Title>
            {result.results.map((item) => {
              const bar = item.status === "deleted" && item.audit_id ? progress[item.audit_id] : undefined;
              return (
                <Flex vertical gap={4} key={item.bridge_id}>
                  <Typography.Text type={item.status === "deleted" ? "success" : "danger"}>
                    {item.status === "deleted" ? "✓" : "✗"} {item.system_number} {item.bridge_name}：
                    {item.status === "deleted" ? "业务档案已完整删除" : item.message}
                  </Typography.Text>
                  {bar && bar.total > 0 ? (
                    <>
                      <Progress
                        size="small"
                        showInfo={false}
                        percent={Math.round((bar.completed / bar.total) * 100)}
                        aria-label={`${item.bridge_name} 归档文件清理进度`}
                        aria-valuemax={bar.total}
                        aria-valuenow={bar.completed}
                      />
                      <Typography.Text type="secondary">
                        {bar.done
                          ? `归档文件已全部清理（共 ${bar.total} 个）`
                          : `已清理 ${bar.completed} / ${bar.total}，剩余 ${bar.pending} 个由后台继续清理`}
                      </Typography.Text>
                    </>
                  ) : null}
                </Flex>
              );
            })}
          </Flex>
        ) : null}
        {error ? <Alert type="error" showIcon title={error} /> : null}
      </Flex>
    </Modal>
  );
}
