import { Alert, Button, Divider, Empty, Flex, Skeleton, Tag, Typography } from "antd";
import { useEffect, useState } from "react";

import type { ArchiveObservation, ComponentDefectArchive, RevisionGroup } from "../api/componentArchiveApi";
import { fetchComponentRevisions } from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { backendBaseUrl } from "../config";
import { ComponentRatingSummary } from "./ComponentRatingSummary";
import { DefectThreadCard } from "./DefectThreadCard";
import { ObservationTable } from "./ObservationTable";
import { RevisionHistoryPanel } from "./RevisionHistoryPanel";

interface ComponentDetailPanelProps {
  archive: ComponentDefectArchive;
  bridgeId: string;
  /** T14：绑定/重绑入口，由页面注入；只读场景可不提供。 */
  onRebind?: (observation: ArchiveObservation) => void;
}

function SectionTitle({ title, count }: { title: string; count?: number }) {
  return (
    <Flex align="center" gap={8}>
      <Typography.Title level={5} style={{ margin: 0 }}>{title}</Typography.Title>
      {count !== undefined ? <Typography.Text type="secondary">{count}</Typography.Text> : null}
      <Divider style={{ flex: 1, minWidth: 0, margin: 0 }} />
    </Flex>
  );
}

// A1 右侧构件档案详情（模块 06 §7.2）：构件基本信息 -> 年度评分摘要 ->
// 病害线索卡片（病害一级、年度二级）-> 未绑定观测区 -> 历史修订独立入口。
export function ComponentDetailPanel({ archive, bridgeId, onRebind }: ComponentDetailPanelProps) {
  const [revisionsOpen, setRevisionsOpen] = useState(false);
  const [revisions, setRevisions] = useState<RevisionGroup[] | null>(null);
  const [revisionsError, setRevisionsError] = useState<string | null>(null);

  // 切换构件时收起历史修订视图并丢弃旧数据。
  useEffect(() => {
    setRevisionsOpen(false);
    setRevisions(null);
    setRevisionsError(null);
  }, [archive.component.id]);

  async function toggleRevisions(): Promise<void> {
    const next = !revisionsOpen;
    setRevisionsOpen(next);
    if (!next || revisions !== null) return;
    try {
      setRevisions(await fetchComponentRevisions(backendBaseUrl, archive.component.id));
      setRevisionsError(null);
    } catch (error) {
      setRevisionsError(error instanceof ApiError ? error.message : "历史修订暂不可用。");
    }
  }

  const { component } = archive;
  return (
    <Flex vertical gap={16}>
      <Flex align="flex-start" justify="space-between" gap={12} wrap>
        <Flex vertical gap={4} style={{ minWidth: 0 }}>
          <Flex align="center" gap={8} wrap>
            <Typography.Title level={4} style={{ margin: 0 }}>{component.business_component_code}</Typography.Title>
            <Tag color="blue" variant="filled">{component.structure_part}</Tag>
            <Tag variant="filled">{component.component_type}</Tag>
          </Flex>
          <Typography.Text type="secondary">构件编号：{component.system_number}</Typography.Text>
        </Flex>
        <Button onClick={() => void toggleRevisions()}>
          {revisionsOpen ? "返回当前档案" : "历史修订"}
        </Button>
      </Flex>

      {revisionsOpen ? (
        <>
          {revisionsError ? <Alert type="error" showIcon title={revisionsError} /> : null}
          {revisions !== null ? <RevisionHistoryPanel revisions={revisions} />
            : revisionsError === null ? <Skeleton active /> : null}
        </>
      ) : (
        <>
          <Flex vertical gap={10}>
            <SectionTitle title="年度评分" />
            <ComponentRatingSummary ratings={archive.ratings} />
          </Flex>

          <Flex vertical gap={10}>
            <SectionTitle title="跨年病害线索" count={archive.threads.length} />
            {archive.threads.length === 0 ? (
              <Empty
                image={Empty.PRESENTED_IMAGE_SIMPLE}
                description={
                  <Flex vertical gap={4}>
                    <Typography.Text strong>尚未形成跨年线索</Typography.Text>
                    <Typography.Text type="secondary">
                      下方 {archive.unbound_observations.length} 条年度观测待确认是否属于同一处病害。
                    </Typography.Text>
                  </Flex>
                }
              >
                <Button type="primary" href={`/bridges/${bridgeId}/defect-threads/triage`}>整理该构件</Button>
              </Empty>
            ) : (
              <Flex vertical gap={12}>
                {archive.threads.map((thread) => (
                  <DefectThreadCard
                    key={thread.id}
                    thread={thread}
                    componentType={component.component_type}
                    onRebind={onRebind}
                  />
                ))}
              </Flex>
            )}
          </Flex>

          <Flex vertical gap={10}>
            <SectionTitle title="待整理的年度观测" count={archive.unbound_observations.length} />
            {archive.unbound_observations.length === 0 ? (
              <Typography.Text type="secondary">当前有效观测均已整理到跨年病害线索。</Typography.Text>
            ) : (
              <ObservationTable observations={archive.unbound_observations} onRebind={onRebind} />
            )}
          </Flex>
        </>
      )}
    </Flex>
  );
}
