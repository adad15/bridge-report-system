import { Alert, Button, Flex, Modal, Table, Typography, theme, type TableColumnsType } from "antd";

import type { ResolutionPlanPreview } from "../../api/resolutionApi";

// 区间展开的确认对话框。
//
// 5.0 起预览由后端生成（§13.3）：前端只展示计划并拿 plan token 去执行，不自己算展开
// 结果。此前两边各算一次，规则一分叉就会出现"预览说能展开、执行时却判无此编号"。
//
// 展示口径也跟着换了。旧预览逐行给"病害数 / 照片数"，那是把展开当成复制病害来算的；
// 新模型里展开只是给同一条来源病害多挂几个解析实例，所以逐行给的是"会绑到几件构件"，
// 总量给的是实例数变化。

type PreviewRow = ResolutionPlanPreview["rows"][number];

const OUTCOME_LABELS: Record<string, string> = {
  will_bind: "将展开绑定",
  will_clear: "将清除绑定",
  will_repoint: "将重指版本",
  skipped: "跳过",
  blocked: "阻断",
};

const REASON_LABELS: Record<string, string> = {
  not_a_range: "该编号不是可展开的构件范围",
  component_not_found: "台账中无此编号",
  component_ambiguous: "台账中有多个同号构件",
  group_already_resolved: "已绑定或已标记缺失，不参与",
};

function outcomeLabel(row: PreviewRow): string {
  if (row.outcome === "will_bind") return OUTCOME_LABELS.will_bind;
  return REASON_LABELS[row.reason_code] ?? row.reason_message ??
    OUTCOME_LABELS[row.outcome] ?? row.outcome;
}

export function ComponentRangeSplitDialog({
  preview,
  loading,
  busy,
  error,
  onClose,
  onRetry,
  onApply,
}: {
  /** null 表示还没预览过。计划一律来自后端，前端不构造。 */
  preview: ResolutionPlanPreview | null;
  loading: boolean;
  busy: boolean;
  error: string | null;
  onClose: () => void;
  onRetry: () => void;
  onApply: (planToken: string) => void;
}) {
  const { token } = theme.useToken();
  const canApply = preview !== null && preview.will_apply_count > 0 && !busy && !loading;

  const columns: TableColumnsType<PreviewRow> = [
    { title: "原构件范围", dataIndex: "source_component_number", key: "number", width: 160 },
    { title: "部件", dataIndex: "source_component_name", key: "name", width: 140, ellipsis: true },
    { title: "病害", dataIndex: "member_count", key: "members", width: 80, align: "center" },
    {
      title: "展开到",
      key: "targets",
      width: 90,
      align: "center",
      render: (_value, row) => row.target_component_ids.length || "—",
    },
    {
      title: "结果",
      key: "outcome",
      render: (_value, row) => (
        <Typography.Text style={{ color: row.outcome === "will_bind" ? token.colorSuccess : token.colorTextSecondary }}>
          {outcomeLabel(row)}
        </Typography.Text>
      ),
    },
  ];

  return (
    <Modal
      open
      title="拆分构件范围"
      width={720}
      onCancel={onClose}
      footer={
        <Flex justify="end" gap={8}>
          <Button disabled={busy} onClick={onClose}>{error ? "关闭" : "取消"}</Button>
          {error ? (
            <Button type="primary" disabled={busy} onClick={onRetry}>重新计算</Button>
          ) : (
            <Button
              type="primary"
              loading={busy}
              disabled={!canApply}
              onClick={() => preview && onApply(preview.plan_token)}
            >
              {busy ? "正在应用…" : "确认拆分"}
            </Button>
          )}
        </Flex>
      }
    >
      <Flex vertical gap={12}>
        <Typography.Text type="secondary">
          展开后每个实际构件分别参与评分，病害数量增加可能使总扣分增加。
          照片整份留在编号最小的那条实例上，其余不带照片——同一个照片编号出现在多条
          观测上，报告里的编号交叉引用就作废了；该配哪张图只有人能判断，请展开后人工挪。
        </Typography.Text>
        {loading ? <Typography.Text type="secondary" role="status">正在计算展开影响…</Typography.Text> : null}
        {error ? <Alert type="error" showIcon role="alert" title={error} /> : null}
        {preview ? (
          <>
            <Table<PreviewRow>
              rowKey="group_id"
              size="small"
              bordered
              columns={columns}
              dataSource={preview.rows}
              pagination={false}
              scroll={{ x: 600, y: 320 }}
            />
            <Typography.Text>
              将展开 {preview.will_apply_count} 个范围
              {preview.skipped_count > 0 ? ` · 跳过 ${preview.skipped_count} 个` : ""}
              {preview.blocked_count > 0 ? ` · 阻断 ${preview.blocked_count} 个` : ""}
              ；解析实例 {preview.instances_before} → {preview.instances_after}
              {preview.rating_recomputed_count > 0
                ? `，重算评分树 ${preview.rating_recomputed_count} 条`
                : ""}
              。
            </Typography.Text>
          </>
        ) : null}
      </Flex>
    </Modal>
  );
}
