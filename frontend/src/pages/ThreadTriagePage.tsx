import { Alert, Button, Card, Empty, Flex, Skeleton, Typography } from "antd";
import { useCallback, useEffect, useState } from "react";
import { Link, useParams } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import type {
  TriageApplyIssue,
  TriageBatchDetail,
  TriageResolvePayload,
  TriageSummary,
} from "../api/threadTriageApi";
import {
  applyTriageBatch,
  buildTriageApplyPayload,
  fetchTriageBatchDetail,
  fetchTriageSummary,
  resolveTriageCluster,
} from "../api/threadTriageApi";
import { PageHeader } from "../design-system";
import { ManualClusterCard } from "../triage/ManualClusterCard";
import { TriageBatchCard } from "../triage/TriageBatchCard";
import { TriageBatchDetailTable } from "../triage/TriageBatchDetail";
import { backendBaseUrl } from "../config";

/**
 * 线索整理工作台。
 *
 * 决策单位是**批次**而不是观测：百股大桥 1197 条观测归成 481 组、144 个批次，一次判断
 * 覆盖 163 个铰缝。旧页面按"一条观测一张卡"设计，冷启动时既没有候选可推、也发不完请求。
 *
 * 状态一律归页面管（设计 §11.4）：批次卡片是纯展示组件，不自行请求、不自行写库。
 */
export function ThreadTriagePage() {
  const { bridgeId } = useParams<{ bridgeId: string }>();
  const [summary, setSummary] = useState<TriageSummary | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [expandedBatchId, setExpandedBatchId] = useState<string | null>(null);
  const [detail, setDetail] = useState<TriageBatchDetail | null>(null);
  const [detailError, setDetailError] = useState<string | null>(null);
  // 剔除与"暂不处理"只在当前会话内生效，刷新后按数据库事实重新计算（设计 §14.4）。
  const [excludedGroupIds, setExcludedGroupIds] = useState<Set<string>>(new Set());
  const [skippedBatchIds, setSkippedBatchIds] = useState<Set<string>>(new Set());
  const [skippedClusterIds, setSkippedClusterIds] = useState<Set<string>>(new Set());
  const [issues, setIssues] = useState<TriageApplyIssue[]>([]);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const reload = useCallback(() => {
    if (!bridgeId) return;
    fetchTriageSummary(backendBaseUrl, bridgeId)
      .then((body) => {
        setSummary(body);
        setError(null);
      })
      .catch((caught) => {
        setError(caught instanceof ApiError ? caught.message : "整理工作台加载失败。");
      });
  }, [bridgeId]);

  useEffect(() => { reload(); }, [reload]);

  async function toggleExpand(batchId: string): Promise<void> {
    if (expandedBatchId === batchId) {
      setExpandedBatchId(null);
      setDetail(null);
      return;
    }
    setExpandedBatchId(batchId);
    setDetail(null);
    setDetailError(null);
    if (!bridgeId) return;
    try {
      // 打开一个批次只拉一次完整明细。
      setDetail(await fetchTriageBatchDetail(backendBaseUrl, bridgeId, batchId));
    } catch (caught) {
      setDetailError(caught instanceof ApiError ? caught.message : "批次明细加载失败。");
    }
  }

  async function confirmBatch(batchId: string): Promise<void> {
    if (!bridgeId) return;
    setBusy(true);
    setIssues([]);
    setNotice(null);
    try {
      // 提交前必须手里有完整清单：没展开过就先取一次明细。
      const loaded = detail?.batch_id === batchId
        ? detail
        : await fetchTriageBatchDetail(backendBaseUrl, bridgeId, batchId);
      const payload = buildTriageApplyPayload(loaded, excludedGroupIds);
      if (payload.groups.length === 0) {
        setIssues([{
          reason_code: "empty_group_selection",
          message: "这一批的组都被剔除了，没有可提交的内容。",
          group_id: null, bridge_component_id: null, observation_id: null,
        }]);
        return;
      }
      const response = await applyTriageBatch(backendBaseUrl, bridgeId, payload);
      setNotice(
        response.status === "already_completed"
          ? `这一批已经处理过：${response.groups_applied} 组。`
          : `已处理 ${response.groups_applied} 组，新建线索 ${response.threads_created} 条，` +
            `绑定观测 ${response.observations_bound} 条。`);
      setExpandedBatchId(null);
      setDetail(null);
      setExcludedGroupIds(new Set());
      reload();
    } catch (caught) {
      if (caught instanceof ApiError && Array.isArray((caught.details as { issues?: unknown })?.issues)) {
        setIssues((caught.details as { issues: TriageApplyIssue[] }).issues);
      } else {
        setIssues([{
          reason_code: "apply_failed",
          message: caught instanceof ApiError ? caught.message : "批量应用失败。",
          group_id: null, bridge_component_id: null, observation_id: null,
        }]);
      }
    } finally {
      setBusy(false);
    }
  }

  async function resolveCluster(payload: TriageResolvePayload): Promise<void> {
    if (!bridgeId) return;
    setBusy(true);
    setIssues([]);
    setNotice(null);
    try {
      const response = await resolveTriageCluster(backendBaseUrl, bridgeId, payload);
      const thread = response.results[0];
      setNotice(
        response.status === "already_completed"
          ? "这一簇已经处理过。"
          : `${payload.action === "create" ? "已新建线索" : "已绑定到线索"} ` +
            `${thread?.thread_system_number ?? ""}，含 ${response.observations_bound} 条观测。`);
      reload();
    } catch (caught) {
      if (caught instanceof ApiError && Array.isArray((caught.details as { issues?: unknown })?.issues)) {
        setIssues((caught.details as { issues: TriageApplyIssue[] }).issues);
      } else {
        setIssues([{
          reason_code: "resolve_failed",
          message: caught instanceof ApiError ? caught.message : "处理失败。",
          group_id: null, bridge_component_id: null, observation_id: null,
        }]);
      }
    } finally {
      setBusy(false);
    }
  }

  if (!bridgeId) return <Alert type="error" showIcon title="缺少桥梁标识。" />;

  const visibleBatches = (summary?.batches ?? []).filter(
    (batch) => !skippedBatchIds.has(batch.batch_id));
  const pendingCount = detail
    ? detail.groups.filter((group) => !excludedGroupIds.has(group.group_id)).length
    : 0;

  return (
    <Flex vertical gap={16}>
      <PageHeader
        title="病害线索整理"
        description={summary
          ? `未绑定 ${summary.unbound_observation_count} 条 · 可批量处理 ${summary.batchable_observation_count} 条 · 异常 ${summary.manual_observation_count} 条`
          : undefined}
        extra={<Link to={`/bridges/${bridgeId}/components`}>返回构件病害档案</Link>}
      />

      {error ? <Alert type="error" showIcon title={error} /> : null}
      {notice ? <Alert type="success" showIcon role="status" title={notice} closable onClose={() => setNotice(null)} /> : null}
      {summary === null && !error ? <Card loading /> : null}

      {summary && summary.batches.length === 0 ? (
        <Card>
          <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="没有可批量处理的批次。" />
        </Card>
      ) : null}

      {visibleBatches.map((batch) => (
        <TriageBatchCard
          key={batch.batch_id}
          batch={batch}
          expanded={expandedBatchId === batch.batch_id}
          busy={busy}
          onToggleExpand={() => { void toggleExpand(batch.batch_id); }}
          onConfirm={() => { void confirmBatch(batch.batch_id); }}
          onSkip={() => setSkippedBatchIds((current) => new Set(current).add(batch.batch_id))}
        >
          {detailError ? <Alert type="error" showIcon title={detailError} /> : null}
          {detail && detail.batch_id === batch.batch_id ? (
            <Flex vertical gap={8}>
              <TriageBatchDetailTable
                detail={detail}
                excludedGroupIds={excludedGroupIds}
                issues={issues}
                onToggleGroup={(groupId) => setExcludedGroupIds((current) => {
                  const next = new Set(current);
                  if (next.has(groupId)) next.delete(groupId);
                  else next.add(groupId);
                  return next;
                })}
              />
              <Typography.Text type="secondary">
                将处理 {pendingCount} 组、
                {detail.groups
                  .filter((group) => !excludedGroupIds.has(group.group_id))
                  .reduce((total, group) => total + group.observations.length, 0)} 条观测
                {excludedGroupIds.size > 0 ? `（已剔除 ${excludedGroupIds.size} 组）` : ""}
              </Typography.Text>
            </Flex>
          ) : expandedBatchId === batch.batch_id && !detailError ? (
            <Skeleton active paragraph={{ rows: 3 }} />
          ) : null}
        </TriageBatchCard>
      ))}

      {summary && summary.manual_clusters.length > 0 ? (
        <Flex vertical gap={12} component="section" aria-label="需单独处理">
          <Flex vertical gap={2}>
            <Typography.Title level={4} style={{ margin: 0 }}>
              需单独处理（{summary.manual_group_count} 组 / {summary.manual_observation_count} 条）
            </Typography.Title>
            <Typography.Text type="secondary">
              这些组之间存在必须一起判断的关系：系统能看出它们有关，但看不出该合还是该分。
            </Typography.Text>
          </Flex>
          {issues.length > 0 ? (
            <Alert
              type="error"
              showIcon
              title="这一簇没能处理"
              description={
                <Flex vertical gap={2}>
                  {issues.map((issue, index) => (
                    <Typography.Text key={`${issue.reason_code}-${index}`}>{issue.message}</Typography.Text>
                  ))}
                </Flex>
              }
            />
          ) : null}
          {summary.manual_clusters
            .filter((cluster) => !skippedClusterIds.has(cluster.cluster_id))
            .map((cluster) => (
              <ManualClusterCard
                key={cluster.cluster_id}
                cluster={cluster}
                bridgeId={bridgeId ?? ""}
                busy={busy}
                onResolve={(payload) => { void resolveCluster(payload); }}
                onSkip={() => setSkippedClusterIds((current) => new Set(current).add(cluster.cluster_id))}
              />
            ))}
        </Flex>
      ) : null}
    </Flex>
  );
}
