import { PlusOutlined, SearchOutlined } from "@ant-design/icons";
import { Button, Card, Empty, Flex, Input, Typography, theme } from "antd";
import { useMemo, useState } from "react";

import type { StandardDocument } from "../api/standardDocumentsApi";
import { VersionStatusTag } from "./versionStatus";

interface Props {
  /** 已按分类、名称排好；null 表示还在加载。 */
  documents: StandardDocument[] | null;
  selectedId: string | null;
  isAdmin: boolean;
  onSelect: (documentId: string) => void;
  onCreate: () => void;
}

/**
 * 左栏规范目录：按分类分组，可按名称或任一版本的编号搜索（设计 §5.2）。
 * 页面不设页头（给阅读区腾高度），管理员的「新增规范」放在这里的标题栏。
 */
export function StandardDocumentList({ documents, selectedId, isAdmin, onSelect, onCreate }: Props) {
  const { token } = theme.useToken();
  const [query, setQuery] = useState("");
  const groups = useMemo(() => {
    const keyword = query.trim().toLocaleLowerCase();
    const visible = (documents ?? []).filter((document) =>
      !keyword
      || document.name.toLocaleLowerCase().includes(keyword)
      || document.versions.some((version) => version.code.toLocaleLowerCase().includes(keyword)));
    const byCategory = new Map<string, StandardDocument[]>();
    for (const document of visible) {
      byCategory.set(document.category, [...(byCategory.get(document.category) ?? []), document]);
    }
    return [...byCategory.entries()];
  }, [documents, query]);

  return (
    <Card
      title="规范目录"
      extra={(
        <Flex align="center" gap={10}>
          <Typography.Text type="secondary">{documents?.length ?? 0} 部</Typography.Text>
          {isAdmin ? <Button type="primary" size="small" icon={<PlusOutlined />} onClick={onCreate}>新增规范</Button> : null}
        </Flex>
      )}
      style={{ height: "100%" }}
      styles={{
        root: { display: "flex", flexDirection: "column" },
        body: { flex: 1, minHeight: 0, display: "flex", flexDirection: "column", gap: 12 },
      }}
    >
      <Input
        allowClear
        aria-label="搜索规范名称或编号"
        prefix={<SearchOutlined aria-hidden="true" />}
        placeholder="搜索规范名称或编号"
        value={query}
        onChange={(event) => setQuery(event.target.value)}
      />
      <Flex vertical gap={16} style={{ flex: 1, minHeight: 0, overflow: "auto" }}>
        {documents !== null && documents.length === 0 ? (
          <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={isAdmin ? "还没有规范，点上方「新增规范」" : "暂无规范"} />
        ) : null}
        {groups.map(([category, items]) => (
          <Flex key={category} vertical gap={2} role="group" aria-label={category}>
            <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM, padding: "0 12px 4px" }}>
              {category}
            </Typography.Text>
            {items.map((document) => {
              const selected = document.id === selectedId;
              const shown = document.versions.find((version) => version.id === document.default_version_id);
              return (
                <Button
                  key={document.id}
                  type="text"
                  block
                  aria-label={document.name}
                  aria-pressed={selected}
                  onClick={() => onSelect(document.id)}
                  style={{
                    height: "auto",
                    padding: "10px 12px",
                    justifyContent: "flex-start",
                    textAlign: "left",
                    background: selected ? token.colorPrimaryBg : undefined,
                  }}
                >
                  <Flex vertical gap={4} style={{ width: "100%", minWidth: 0 }}>
                    <Flex align="center" justify="space-between" gap={8}>
                      <Typography.Text strong ellipsis style={{ color: selected ? token.colorPrimary : undefined }}>
                        {document.name}
                      </Typography.Text>
                      {shown ? <VersionStatusTag status={shown.status} /> : <Typography.Text type="secondary">暂无版本</Typography.Text>}
                    </Flex>
                    {shown ? (
                      <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                        {shown.code}
                        {document.versions.length > 1 ? ` · ${document.versions.length} 个版本` : ""}
                      </Typography.Text>
                    ) : null}
                  </Flex>
                </Button>
              );
            })}
          </Flex>
        ))}
      </Flex>
    </Card>
  );
}
