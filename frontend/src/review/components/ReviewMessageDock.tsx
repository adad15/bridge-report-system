import { Alert, Flex, Typography } from "antd";

import type { ApiErrorIssue } from "../../api/apiClient";

// 保存草稿 / 入库前检查 / 确认入库失败后展示给用户的提示；成功也复用同一个状态
// （kind="success"）显示"已保存"这类短暂反馈。issues 只有 400 契约校验失败
// （code=contract_validation_failed）时后端才会填，逐条列出 {path, message}。
export interface SaveMessageState {
  kind: "success" | "error";
  text: string;
  issues?: ApiErrorIssue[];
}

interface ReviewMessageDockProps {
  saveMessage: SaveMessageState | null;
  onDismissSaveMessage: () => void;
}

// 停靠在底部操作栏上方的消息面板：只负责保存 / 确认这类一次性反馈。入库前检查结果
// 已移进操作栏那一行——它跟「入库前检查」「确认入库」两个按钮是同一件事，分成两行看
// 反而要来回找。纯展示组件，成功消息的消失时机归页面层管。
export function ReviewMessageDock({ saveMessage, onDismissSaveMessage }: ReviewMessageDockProps) {
  if (!saveMessage) return null;
  const saveMessageText = saveMessage.text.trim()
    || (saveMessage.kind === "error" ? "操作失败，请稍后重试。" : "操作已完成。");

  return (
    <Alert
      type={saveMessage.kind === "error" ? "error" : "success"}
      showIcon
      role={saveMessage.kind === "error" ? "alert" : "status"}
      title={saveMessageText}
      description={saveMessage.issues && saveMessage.issues.length > 0 ? (
        <Flex vertical gap={2}>
          {saveMessage.issues.map((issue, index) => (
            <Typography.Text key={`${issue.path}-${index}`} type="danger">
              {issue.path}: {issue.message}
            </Typography.Text>
          ))}
        </Flex>
      ) : undefined}
      closable={saveMessage.kind === "error" ? { "aria-label": "关闭消息" } : false}
      onClose={onDismissSaveMessage}
      style={{ margin: "0 16px" }}
    />
  );
}
