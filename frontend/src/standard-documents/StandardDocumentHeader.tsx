import { DownloadOutlined, MoreOutlined, UploadOutlined } from "@ant-design/icons";
import { Alert, Button, Dropdown, Flex, Select, Typography, theme } from "antd";

import { type StandardDocument, type StandardVersion, standardVersionContentUrl } from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";
import { VersionStatusTag, isDraft, shortIssuer, versionDateText } from "./versionStatus";

interface Props {
  document: StandardDocument;
  /** 没有版本时为空。 */
  version: StandardVersion | null;
  isAdmin: boolean;
  onSelectVersion: (versionId: string) => void;
  /** 给这部规范加版本。放在头部而不是版本记录页签里：实测时管理员找不到入口，转身去点了「新增规范」。 */
  onUploadVersion: () => void;
  onEditDocument: () => void;
  onDeleteDocument: () => void;
}

/** 非现行版本的提示（设计 §5.3）；现行版本返回空。 */
function noticeFor(document: StandardDocument, version: StandardVersion): string | null {
  const current = document.versions.find((item) => item.status === "current");
  if (isDraft(version)) return `${version.label}尚未正式发布，内容可能与最终发布版本不同，不能作为评定依据。`;
  if (version.status === "withdrawn") {
    const replacement = document.versions.find((item) => item.id === version.replaced_by_version_id);
    return replacement
      ? `${version.code} 已于 ${version.withdrawn_date} 废止，由 ${replacement.code} 替代；仅供查阅历史报告的评定依据。`
      : `${version.code} 已于 ${version.withdrawn_date} 废止；仅供查阅历史报告的评定依据。`;
  }
  if (version.status === "upcoming") {
    return current
      ? `${version.code} 将于 ${version.effective_date} 实施。在此之前现行版本仍是 ${current.code}。`
      : `${version.code} 将于 ${version.effective_date} 实施。`;
  }
  return null;
}

export function StandardDocumentHeader({
  document, version, isAdmin, onSelectVersion, onUploadVersion, onEditDocument, onDeleteDocument,
}: Props) {
  const { token } = theme.useToken();
  const current = document.versions.find((item) => item.status === "current");
  const official = document.versions.filter((item) => !isDraft(item));
  const drafts = document.versions.filter(isDraft);
  const issuer = version ? shortIssuer(version.issuer) : null;
  const facts = version
    ? [
        version.code,
        isDraft(version) ? "尚未发布" : issuer ? `${issuer}发布` : null,
        versionDateText(version),
        // 现行版本已有接替者时，废止日期还在将来。
        !isDraft(version) && version.withdrawn_date
          ? version.status === "withdrawn" ? `${version.withdrawn_date} 废止` : `将于 ${version.withdrawn_date} 废止`
          : null,
        version.announcement,
      ].filter((item): item is string => Boolean(item))
    : [];
  const notice = version ? noticeFor(document, version) : null;
  const toOption = (item: StandardVersion) => ({ value: item.id, label: item.label });

  // 头部压成两行，给阅读区腾高度（设计 §5.3）：规范名、状态和版本切换一行，编号与日期一行。
  // 分类在左栏的分组标题里已经有了，标准性质在「基本信息」里，这里都不再重复。
  return (
    <Flex vertical gap={8}>
      <Flex vertical gap={2}>
        <Flex align="center" justify="space-between" gap={16} wrap>
          <Flex align="center" gap={10} style={{ minWidth: 0 }}>
            <Typography.Title level={4} style={{ margin: 0 }}>{document.name}</Typography.Title>
            {version ? <VersionStatusTag status={version.status} /> : null}
          </Flex>
          <Flex align="center" gap={8} wrap>
            {version ? (
              <>
                <Typography.Text type="secondary">版本</Typography.Text>
                <Select
                  aria-label="版本"
                  value={version.id}
                  onChange={onSelectVersion}
                  style={{ width: 260 }}
                  popupMatchSelectWidth={340}
                  listHeight={360}
                  labelRender={({ value }) => {
                    const item = document.versions.find((candidate) => candidate.id === value);
                    return item ? (
                      <Flex align="center" gap={8}>
                        <span>{item.label}</span>
                        <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>{item.code}</Typography.Text>
                      </Flex>
                    ) : null;
                  }}
                  optionRender={(option) => {
                    const item = document.versions.find((candidate) => candidate.id === option.value);
                    if (!item) return option.label;
                    return (
                      <Flex align="center" justify="space-between" gap={12}>
                        <Flex vertical>
                          <span>{item.label}</span>
                          <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                            {[item.code, versionDateText(item)].filter(Boolean).join(" · ")}
                          </Typography.Text>
                        </Flex>
                        <VersionStatusTag status={item.status} />
                      </Flex>
                    );
                  }}
                  options={[
                    ...(official.length ? [{ label: "正式版本", title: "正式版本", options: official.map(toOption) }] : []),
                    ...(drafts.length ? [{ label: "修订稿（未发布）", title: "修订稿（未发布）", options: drafts.map(toOption) }] : []),
                  ]}
                />
                <Button icon={<DownloadOutlined />} href={standardVersionContentUrl(backendBaseUrl, version, { download: true })}>
                  下载原文
                </Button>
              </>
            ) : null}
            {isAdmin ? <Button icon={<UploadOutlined />} onClick={onUploadVersion}>上传新版本</Button> : null}
            {isAdmin ? (
              <Dropdown
                trigger={["click"]}
                menu={{
                  items: [
                    { key: "edit", label: "编辑规范信息" },
                    { key: "delete", label: "删除规范", danger: true },
                  ],
                  onClick: ({ key }) => (key === "edit" ? onEditDocument() : onDeleteDocument()),
                }}
              >
                <Button icon={<MoreOutlined />} aria-label="更多">更多</Button>
              </Dropdown>
            ) : null}
          </Flex>
        </Flex>
        {facts.length > 0 ? <Typography.Text type="secondary">{facts.join(" · ")}</Typography.Text> : null}
      </Flex>
      {notice ? (
        <Alert
          type="warning"
          showIcon
          title={notice}
          action={current && version && current.id !== version.id ? (
            <Button size="small" onClick={() => onSelectVersion(current.id)}>查看现行版本</Button>
          ) : undefined}
        />
      ) : null}
    </Flex>
  );
}
