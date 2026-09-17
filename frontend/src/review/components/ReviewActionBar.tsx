import { useState } from "react";
import { Button, Flex, Popover, Typography, theme } from "antd";
import { CheckCircleOutlined, DownOutlined, UpOutlined, WarningOutlined } from "@ant-design/icons";

import type { PreflightResponse } from "../../api/reviewApi";
import { formatIssue } from "../preflightIssueText";

interface ReviewActionBarProps {
  onSaveDraft?: () => void;
  onPreflight?: () => void;
  onConfirmImport?: () => void;
  onCancelImport?: () => void;
  /** 重开校对态：替代"取消导入"的「放弃修改」（还原为已确认时草稿）。 */
  onAbandonReopen?: () => void;
  dirty?: boolean;
  readOnlyNotice?: string;
  onBackToBridge?: () => void;
  backLabel?: string;
  /** 已确认只读态的重开入口：存在带警告病害时对所有登录用户开放。 */
  onReopenWarnings?: () => void;
  /** 已确认只读态的重开入口：仅管理员（解锁全部修改）。 */
  onReopenFull?: () => void;
  /** 入库前检查结果。摘要显示在本栏中部，明细展开成本栏上方的浮层。 */
  preflight?: PreflightResponse | null;
  preflightTargetLabels?: ReadonlyMap<string, string>;
}

// 底部操作栏（模块 05 §7.5，布局见 2026-07-12 布局设计 §9）。本组件保持纯展示：一个按钮
// 是否可用完全取决于调用方（ReviewWorkspacePage）是否传了对应的 handler——不传 -> 禁用。
// 业务逻辑、加载中状态和只读状态全部在页面层维护（只读时页面层传 readOnlyNotice，本栏
// 整条换成只读横幅 + 返回按钮 + 可选的重开校对入口）。
export function ReviewActionBar({
  onSaveDraft,
  onPreflight,
  onConfirmImport,
  onCancelImport,
  onAbandonReopen,
  dirty = false,
  readOnlyNotice,
  onBackToBridge,
  backLabel = "返回桥梁详情",
  onReopenWarnings,
  onReopenFull,
  preflight = null,
  preflightTargetLabels = new Map(),
}: ReviewActionBarProps) {
  const { token } = theme.useToken();
  const [preflightExpanded, setPreflightExpanded] = useState(false);
  const preflightIssueCount = preflight
    ? preflight.blocking_errors.length + preflight.warnings.length
    : 0;

  const barStyle = {
    padding: "10px 16px",
    borderTop: `1px solid ${token.colorSplit}`,
    background: token.colorBgContainer,
  };

  if (readOnlyNotice) {
    return (
      <Flex align="center" gap={10} wrap style={barStyle}>
        <Typography.Text type="secondary" style={{ flex: 1, minWidth: 200 }}>{readOnlyNotice}</Typography.Text>
        {onReopenWarnings ? <Button onClick={onReopenWarnings}>修正警告病害</Button> : null}
        {onReopenFull ? <Button onClick={onReopenFull}>解锁全部修改</Button> : null}
        <Button onClick={onBackToBridge}>{backLabel}</Button>
      </Flex>
    );
  }

  return (
    <Flex align="center" gap={12} wrap style={barStyle}>
      <Button disabled={!onBackToBridge} onClick={onBackToBridge}>{backLabel}</Button>

      {dirty ? (
        <Typography.Text type="warning">● 有未保存的修改，请先保存草稿再进行入库前检查</Typography.Text>
      ) : null}

      {preflight ? (
        <Flex align="center" gap={6}>
          <Typography.Text type={preflight.can_confirm ? "success" : "warning"}>
            {preflight.can_confirm ? <CheckCircleOutlined /> : <WarningOutlined />}
          </Typography.Text>
          <Typography.Text strong>
            {preflight.can_confirm
              ? "检查通过，可以确认入库。"
              : `入库前检查：${preflight.blocking_errors.length} 个阻断项 · ${preflight.warnings.length} 个提醒`}
          </Typography.Text>
          {preflightIssueCount > 0 ? (
            /* 明细浮在本栏上方，不把操作栏顶高，也不再另起一行。 */
            <Popover
              open={preflightExpanded}
              placement="top"
              trigger="click"
              // 收起后要真的从 DOM 里移掉：留着隐藏的明细，读屏和测试都还会读到它。
              destroyOnHidden
              onOpenChange={setPreflightExpanded}
              content={
                <Flex vertical gap={4} style={{ maxWidth: 520, maxHeight: 320, overflowY: "auto" }}>
                  {preflight.blocking_errors.map((issue, index) => (
                    <Typography.Text key={`blocking-${index}`} type="danger">
                      {formatIssue(issue, preflightTargetLabels)}
                    </Typography.Text>
                  ))}
                  {preflight.warnings.map((issue, index) => (
                    <Typography.Text key={`warning-${index}`} type="warning">
                      {formatIssue(issue, preflightTargetLabels)}
                    </Typography.Text>
                  ))}
                </Flex>
              }
            >
              <Button
                type="text"
                size="small"
                aria-expanded={preflightExpanded}
                aria-label={preflightExpanded ? "收起入库前检查明细" : "展开入库前检查明细"}
                icon={preflightExpanded ? <UpOutlined /> : <DownOutlined />}
              />
            </Popover>
          ) : null}
        </Flex>
      ) : null}

      {/* 取消导入 / 保存草稿 / 入库前检查 / 确认入库是同一组收口动作，成组靠右；
          左边只留「返回」。中间放不下时整组一起换行，不会拆成两截。 */}
      <Flex align="center" gap={8} wrap style={{ marginInlineStart: "auto" }}>
        {onAbandonReopen ? (
          <Button danger onClick={onAbandonReopen}>放弃修改</Button>
        ) : (
          <Button danger disabled={!onCancelImport} onClick={onCancelImport}>取消导入</Button>
        )}
        <Button disabled={!onSaveDraft} onClick={onSaveDraft}>保存草稿</Button>
        <Button disabled={!onPreflight} onClick={onPreflight}>入库前检查</Button>
        <Button type="primary" disabled={!onConfirmImport} onClick={onConfirmImport}>确认年度事实入库</Button>
      </Flex>
    </Flex>
  );
}
