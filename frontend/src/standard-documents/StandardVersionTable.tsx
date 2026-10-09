import { UploadOutlined } from "@ant-design/icons";
import { Button, Flex, Popconfirm, Table, Typography, theme } from "antd";

import { type StandardDocument, type StandardVersion, standardVersionContentUrl } from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";
import { VersionStatusTag, isDraft } from "./versionStatus";

interface Props {
  document: StandardDocument;
  viewingId: string | null;
  isAdmin: boolean;
  onRead: (versionId: string) => void;
  onUpload: () => void;
  onEdit: (version: StandardVersion) => void;
  onReplace: (version: StandardVersion) => void;
  /** 返回 Promise：确认框在删除完成前保持加载状态。 */
  onDelete: (version: StandardVersion) => Promise<void>;
}

/** 说明栏：替代关系、公告文号、备注，取第一个有值的（设计 §5.5）。 */
function noteText(document: StandardDocument, version: StandardVersion): string {
  const codeOf = (id: string | null) => document.versions.find((item) => item.id === id)?.code;
  const replacedBy = codeOf(version.replaced_by_version_id);
  if (version.status === "withdrawn" && replacedBy) return `${version.withdrawn_date} 起由 ${replacedBy} 替代`;
  const replaces = codeOf(version.replaces_version_id);
  if (replaces) return `替代 ${replaces}`;
  return version.announcement ?? version.note ?? "—";
}

export function StandardVersionTable({ document, viewingId, isAdmin, onRead, onUpload, onEdit, onReplace, onDelete }: Props) {
  const { token } = theme.useToken();
  return (
    <Flex vertical gap={12}>
      <Flex align="center" justify="space-between" gap={12} wrap>
        <Typography.Text type="secondary">
          共 {document.versions.length} 个版本。征求意见稿、报批稿仅供对照查阅，不作为评定依据。
        </Typography.Text>
        {isAdmin ? <Button icon={<UploadOutlined />} onClick={onUpload}>上传新版本</Button> : null}
      </Flex>
      <Table<StandardVersion>
        rowKey="id"
        pagination={false}
        dataSource={document.versions}
        columns={[
          {
            title: "版本",
            key: "label",
            width: 150,
            render: (_, version) => (
              <Flex vertical>
                <Typography.Text strong>{version.label}</Typography.Text>
                {version.id === viewingId ? (
                  <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>正在查看</Typography.Text>
                ) : null}
              </Flex>
            ),
          },
          { title: "标准编号", dataIndex: "code", width: 180 },
          { title: "状态", key: "status", width: 120, render: (_, version) => <VersionStatusTag status={version.status} /> },
          {
            title: "发布 / 实施",
            key: "dates",
            width: 190,
            render: (_, version) => {
              if (isDraft(version)) {
                return <Typography.Text type="secondary">{version.publish_date ? `稿件日期 ${version.publish_date}` : "未发布"}</Typography.Text>;
              }
              return (
                <Flex vertical>
                  <span>{version.publish_date ?? "—"} 发布</span>
                  <Typography.Text type="secondary">{version.effective_date ?? "—"} 实施</Typography.Text>
                </Flex>
              );
            },
          },
          {
            title: "说明",
            key: "note",
            render: (_, version) => (
              <Flex vertical>
                <span>{noteText(document, version)}</span>
                <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                  原文来源：{version.source ?? "—"}
                </Typography.Text>
              </Flex>
            ),
          },
          {
            title: "操作",
            key: "actions",
            width: isAdmin ? 260 : 120,
            render: (_, version) => (
              <Flex gap={12} style={{ whiteSpace: "nowrap" }}>
                <Typography.Link onClick={() => onRead(version.id)}>阅读</Typography.Link>
                <Typography.Link href={standardVersionContentUrl(backendBaseUrl, version, { download: true })}>下载</Typography.Link>
                {isAdmin ? (
                  <>
                    <Typography.Link onClick={() => onEdit(version)}>编辑</Typography.Link>
                    <Typography.Link onClick={() => onReplace(version)}>替换原文</Typography.Link>
                    <Popconfirm
                      title={`删除「${version.label}」？`}
                      description="原文文件会一并删除，无法恢复。"
                      okText="删除"
                      okButtonProps={{ danger: true }}
                      cancelText="取消"
                      onConfirm={() => onDelete(version)}
                    >
                      <Typography.Link type="danger">删除</Typography.Link>
                    </Popconfirm>
                  </>
                ) : null}
              </Flex>
            ),
          },
        ]}
      />
    </Flex>
  );
}
