import { Alert, Button, Flex, Form, Input, Modal, Table, Typography, theme, type TableColumnsType } from "antd";
import { useEffect, useRef, useState } from "react";

import type { ResolutionPlanPreview } from "../../api/resolutionApi";

// 批量查找替换。设计见
// docs/superpowers/specs/2026-07-24-bulk-binding-replace-design.md §3 与
// docs/superpowers/specs/2026-08-27-import-component-rating-resolution-separation-design.md §13.3。
//
// 5.0 起**预览由后端生成**：前端把查找/替换串交上去换一份计划，应用时只提交
// plan token，不提交自己算出来的结果集合。这样"用户看到的计划"与"实际执行的计划"
// 天然是同一份——此前两边各算一次，规则一分叉就会出现"预览说能绑、后端却判无此编号"。

type PreviewRow = ResolutionPlanPreview["rows"][number];

const OUTCOME_LABELS: Record<string, string> = {
  will_bind: "将绑定",
  will_clear: "将清除绑定",
  will_repoint: "将重指版本",
  skipped: "跳过",
  blocked: "阻断",
};

// 行级原因码由后端给，前端只做展示措辞，不自己判断为什么跳过。
const REASON_LABELS: Record<string, string> = {
  pattern_not_matched: "不符合查找模式",
  component_not_found: "台账中无此编号",
  component_ambiguous: "台账中有多个同号构件",
  group_already_resolved: "已绑定或已标记缺失，不参与",
  not_a_range: "该编号不是可展开的构件范围",
};

/* 查找串停顿多久就去取一次预览。太短会把每个按键都打成一次后端请求，
   太长又会让人以为没反应；400ms 是"手停下来"的常见阈值。 */
const PREVIEW_DEBOUNCE_MS = 400;

function outcomeLabel(row: PreviewRow): string {
  if (row.outcome === "will_bind") return OUTCOME_LABELS.will_bind;
  return REASON_LABELS[row.reason_code] ?? row.reason_message ??
    OUTCOME_LABELS[row.outcome] ?? row.outcome;
}

export function BulkReplaceDialog({
  partName,
  plan,
  previewing,
  busy,
  error,
  onPreview,
  onClearPlan,
  onApply,
  onClose,
}: {
  partName: string;
  /** null 表示还没预览过。计划一律来自后端，前端不构造。 */
  plan: ResolutionPlanPreview | null;
  previewing: boolean;
  busy: boolean;
  error?: string | null;
  onPreview: (find: string, replace: string) => void | Promise<void>;
  /** 查找串清空时丢掉上一份计划，免得空条件下还挂着旧预览。 */
  onClearPlan: () => void;
  onApply: (planToken: string) => void | Promise<void>;
  onClose: () => void;
}) {
  const { token } = theme.useToken();
  const [find, setFind] = useState("");
  const [replace, setReplace] = useState("");

  /* 回调每次渲染都是新的匿名函数，放进依赖会让 effect 每帧重跑；用 ref 取最新的一份，
     依赖里只留真正的输入。 */
  const previewRef = useRef(onPreview);
  const clearRef = useRef(onClearPlan);
  previewRef.current = onPreview;
  clearRef.current = onClearPlan;

  // 不再要求先点一次「生成预览」：输入停下就自动去取计划。
  useEffect(() => {
    if (find.trim() === "") {
      clearRef.current();
      return;
    }
    const timer = window.setTimeout(() => {
      void previewRef.current(find, replace);
    }, PREVIEW_DEBOUNCE_MS);
    return () => window.clearTimeout(timer);
  }, [find, replace]);

  const canApply = plan !== null && plan.will_apply_count > 0 && !busy && !previewing;

  const columns: TableColumnsType<PreviewRow> = [
    { title: "报告编号", dataIndex: "source_component_number", key: "source", ellipsis: true },
    {
      title: "转换后",
      key: "resolved",
      ellipsis: true,
      render: (_value, row) => row.resolved_numbers[0] ?? "—",
    },
    {
      title: "结果",
      key: "outcome",
      width: 190,
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
      title={`批量替换 · ${partName}`}
      width={680}
      onCancel={onClose}
      footer={
        <Flex justify="end" gap={8}>
          <Button disabled={busy} onClick={onClose}>取消</Button>
          <Button
            type="primary"
            loading={busy}
            disabled={!canApply}
            onClick={() => plan && void onApply(plan.plan_token)}
          >
            {busy ? "正在应用…" : "应用"}
          </Button>
        </Flex>
      }
    >
      <Flex vertical gap={12}>
        <Typography.Text type="secondary">
          用 <Typography.Text code>*</Typography.Text> 代表一段数字。例：查找{" "}
          <Typography.Text code>第*孔桥面</Typography.Text>、替换为{" "}
          <Typography.Text code>*#跨桥面铺装</Typography.Text>
          。替换结果只用于查找台账构件，报告原文不会被改写。
        </Typography.Text>

        <Form layout="vertical" style={{ marginBottom: 0 }}>
          <Flex gap={12} wrap>
            <Form.Item label="查找" htmlFor="bulk-replace-find" style={{ flex: 1, minWidth: 200, marginBottom: 0 }}>
              <Input
                id="bulk-replace-find"
                value={find}
                disabled={busy}
                onChange={(event) => setFind(event.target.value)}
              />
            </Form.Item>
            <Form.Item label="替换为" htmlFor="bulk-replace-to" style={{ flex: 1, minWidth: 200, marginBottom: 0 }}>
              <Input
                id="bulk-replace-to"
                value={replace}
                disabled={busy}
                onChange={(event) => setReplace(event.target.value)}
              />
            </Form.Item>
          </Flex>
        </Form>

        {previewing ? <Typography.Text type="secondary" role="status">正在生成预览…</Typography.Text> : null}
        {error ? <Alert type="error" showIcon role="alert" title={error} /> : null}

        {plan ? (
          <>
            <Table<PreviewRow>
              rowKey="group_id"
              size="small"
              bordered
              columns={columns}
              dataSource={plan.rows}
              pagination={false}
              scroll={{ x: 520, y: 300 }}
            />
            <Typography.Text>
              将绑定 {plan.will_apply_count} 行 · 跳过 {plan.skipped_count} 行
              {plan.blocked_count > 0 ? ` · 阻断 ${plan.blocked_count} 行` : ""}
            </Typography.Text>
          </>
        ) : null}
      </Flex>
    </Modal>
  );
}
