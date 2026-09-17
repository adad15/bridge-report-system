import { useEffect, useMemo, useState, type Dispatch } from "react";
import {
  Alert,
  Button,
  Card,
  Descriptions,
  Divider,
  Flex,
  Form,
  Input,
  Select,
  Tag,
  Tooltip,
  Typography,
  theme,
} from "antd";
import {
  CheckCircleFilled,
  CloseOutlined,
  DownOutlined,
  ExclamationCircleFilled,
  StopOutlined,
  UndoOutlined,
  UpOutlined,
} from "@ant-design/icons";

import {
  fetchComponentArchive,
  type ArchiveObservation,
  type ComponentDefectArchive,
} from "../../api/componentArchiveApi";
import {
  fetchRatingTreeNode,
  ratingTreeErrorMessage,
  type RatingTreeNode,
  type RatingTreeNodeSummary,
} from "../../api/ratingTreeApi";
import { ApiError } from "../../api/apiClient";
import { applySourceRatingResolution } from "../../api/resolutionApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import {
  ratingTreeOptionLabel,
  sortRatingTreeNodes,
} from "../../rating-tree/ratingTreeLabels";
import { matchMethodLabel, type DefectReviewRow } from "../defectPhotoReviewModel";
import type { ReviewDraftAction } from "../reviewDraft";
import { displayDefectLocation, displayMatchEvidence } from "./displayHelpers";
import { DefectPhotoPanel } from "./DefectPhotoPanel";

interface DefectDetailEditorProps {
  draft: BridgeAnnualInspectionData;
  row: DefectReviewRow;
  ratingTreeVersionId: string | null;
  applicableNodes: RatingTreeNodeSummary[];
  importRecordId: string;
  baseUrl: string;
  bridgeId?: string;
  initialPhotoCandidateId?: string | null;
  dispatch: Dispatch<ReviewDraftAction>;
  disabled?: boolean;
  allowDelete?: boolean;
  editLockToken?: string | null;
  onSave?: () => void;
  onConfirm: () => void;
  onClose: () => void;
  /** 当前筛选结果里的上一条 / 下一条；到头了不传，按钮置灰。 */
  onPrevious?: () => void;
  onNext?: () => void;
  /**
   * 病害类型/描述这类影响匹配的文字提交后触发重新匹配。只在失焦时调用，
   * 不能每敲一个键就发一次请求。
   */
  onDefectTextCommitted?: (candidateId: string) => void;
  /**
   * 人工选定评定树节点后触发，用于重取解析工作区。
   *
   * 节点住在评分树解析表里，不在草稿里（5.0），所以选完必须让工作区刷新一次，
   * 否则界面还显示旧结果，而下一次写会拿着过期版本撞冲突。
   */
  onRatingResolved?: () => void;
  /** 当前工作区所依据的台账版本，随裁决一起提交做前提校验。 */
  inventoryRevisionId?: string | null;
}

export function DefectDetailEditor({
  draft,
  row,
  ratingTreeVersionId,
  applicableNodes,
  importRecordId,
  baseUrl,
  bridgeId,
  initialPhotoCandidateId,
  dispatch,
  disabled = false,
  allowDelete = false,
  editLockToken,
  onSave,
  onConfirm,
  onClose,
  onPrevious,
  onNext,
  onDefectTextCommitted,
  onRatingResolved,
  inventoryRevisionId,
}: DefectDetailEditorProps) {
  const defect = row.defect;
  const [treeNode, setTreeNode] = useState<RatingTreeNode | null>(row.ratingTreeNode);
  const [treeNodeError, setTreeNodeError] = useState("");
  const [componentArchive, setComponentArchive] = useState<ComponentDefectArchive | null>(null);
  const [historyLoading, setHistoryLoading] = useState(false);
  const [historyUnavailable, setHistoryUnavailable] = useState(false);
  const orderedApplicableNodes = useMemo(
    () => sortRatingTreeNodes(applicableNodes),
    [applicableNodes],
  );
  const selectedNodeSummary = applicableNodes.find(
    (item) => item.id === row.resolution.ratingTreeNodeId,
  ) ?? null;
  const activeTreeNode = treeNode?.id === row.resolution.ratingTreeNodeId ? treeNode : null;
  const selectedNodeIsScoring = activeTreeNode?.is_scoring ?? selectedNodeSummary?.is_scoring ?? false;
  const selectedNodeAllowedScales = activeTreeNode?.allowed_scales ?? selectedNodeSummary?.allowed_scales ?? [];
  const selectedNodeScaleDescriptions = activeTreeNode?.scale_descriptions ?? selectedNodeSummary?.scale_descriptions ?? {};
  const currentScale = typeof defect.defect_scale === "number" ? defect.defect_scale : null;
  const boundComponentId = row.resolution.bridgeComponentId
    ?? (row.resolution.componentIds.length === 1 ? row.resolution.componentIds[0] : null);
  const archiveHref = bridgeId
    ? `/bridges/${encodeURIComponent(bridgeId)}/components${boundComponentId ? `/${encodeURIComponent(boundComponentId)}` : ""}`
    : null;
  // 候选是后端这次算出来的临时结果，不在草稿里；采用候选后按人工选择处理。
  // 5.0 起匹配证据不再写回草稿，展示的就是本次匹配给出的原因。
  const evidence = displayMatchEvidence(
    row.matchResult?.reason_message,
    row.resolution.ratingMatchMethod,
  );
  // 尺寸原文只是解析用的输入，本身没有单独展示的价值；展示解析结果即可。
  // 解析不出来时才退回原文，那种情况下原文是唯一的记录。
  const measurementSummary = defect.measurements.length > 0
    ? defect.measurements
        .map((item) => {
          const value = item.value_type === "range"
            ? `${item.minimum_value ?? "?"}~${item.maximum_value ?? "?"}`
            : `${item.value ?? "?"}`;
          return `${item.dimension_type} ${value}${item.unit}`;
        })
        .join("、")
    : defect.measurement_text ?? "";
  const [ratingError, setRatingError] = useState("");
  const { token } = theme.useToken();

  const historicalObservations = useMemo(() => {
    if (!componentArchive) return [];
    const currentType = (activeTreeNode?.display_name
      ?? selectedNodeSummary?.display_name
      ?? defect.defect_type).trim();
    const observations = [
      ...componentArchive.threads.flatMap((thread) => thread.observations),
      ...componentArchive.unbound_observations,
    ];
    const unique = new Map<string, ArchiveObservation>();
    for (const observation of observations) {
      if (
        observation.inspection_year < draft.inspection.inspection_year
        && observation.defect_type.trim() === currentType
      ) {
        unique.set(observation.id, observation);
      }
    }
    return [...unique.values()]
      .sort((left, right) => right.inspection_year - left.inspection_year)
      .slice(0, 2);
  }, [activeTreeNode?.display_name, componentArchive, defect.defect_type, draft.inspection.inspection_year, selectedNodeSummary?.display_name]);

  /* 左列要把已确定的规范病害只读回显一次。上方那行是赋值入口（常驻下拉），
     这里是回显，取的是下拉选中项的同一份文案，免得两处措辞对不上。 */
  const resolvedNodeLabel = (() => {
    const nodeId = row.resolution.ratingTreeNodeId;
    if (!nodeId) return "";
    const node = orderedApplicableNodes.find((item) => item.id === nodeId);
    return node ? ratingTreeOptionLabel(node, orderedApplicableNodes) : "";
  })();

  /* 这三项从维护栏撤下后并进了底部操作行：它们回答的是「这条现在能不能确认」，
     跟「确认」是同一个决定，放在按钮旁边比单独占一块更省地方。 */
  const completenessItems = [
    { label: "构件已匹配", complete: row.resolution.componentIds.length > 0 || Boolean(row.resolution.bridgeComponentId) },
    { label: "评定项已确定", complete: Boolean(row.resolution.ratingTreeNodeId) },
    {
      label: "照片已关联",
      complete: row.photoCards.length > 0
        && row.photoCards.every((card) => card.kind === "photo" || card.acknowledgedMissing),
    },
  ];

  const completeness = Math.round(
    completenessItems.filter((item) => item.complete).length / completenessItems.length * 100,
  );

  const latestHistorical = historicalObservations[0] ?? null;
  const latestHistoricalScale = latestHistorical?.scale ? Number(latestHistorical.scale) : null;
  const scaleTrend = latestHistoricalScale !== null && Number.isFinite(latestHistoricalScale)
    && currentScale !== null
    ? currentScale > latestHistoricalScale
      ? "标度上升，建议重点关注"
      : currentScale < latestHistoricalScale
        ? "标度下降，请核对是否已维修"
        : "标度未变，建议持续观察"
    : "已有历史记录，可结合照片判断变化";

  const selectNode = (nodeId: string) => {
    const selected = applicableNodes.find((item) => item.id === nodeId);
    if (!selected || !ratingTreeVersionId) return;

    // 节点本身写进评分树解析表（§9.2）。不写的话，用户的显式选择只活在这一次渲染里：
    // 刷新页面或下一次同步，这条病害要么被自动匹配重新盖掉，要么退回未解析——
    // 人工判断丢得无声无息。
    if (!editLockToken) {
      setRatingError("当前页面没有编辑权，节点选择未保存。");
      return;
    }
    const instances = row.resolution.instances;
    if (instances.length === 0) {
      setRatingError("这条病害还没绑定实际构件，无法保存评定树选择。");
      return;
    }
    setRatingError("");
    void (async () => {
      try {
        // 一次请求写完这条病害的全部实例。逐条发的话，后端每次都要取草稿、装评定树、
        // 鉴权、查编辑锁、开事务、提交——区间展开的病害是 25 次，实测 2 秒。
        // 而且同一事务写完才有意义：这一行显示的是整条病害的结论，写进去一半更难查。
        await applySourceRatingResolution(
          baseUrl,
          importRecordId,
          defect.candidate_id,
          {
            instances: instances.map((instance) => ({
              instance_id: instance.instanceId,
              expected_version: instance.ratingVersion,
            })),
            rating_tree_node_id: selected.id,
            expected_rating_tree_version_id: ratingTreeVersionId,
            // 台账版本变了，"这个节点适不适用于该构件"的答案就可能变；不带上它，
            // 旧页面能把一个基于过时映射的判断写进去。
            ...(inventoryRevisionId ? { expected_inventory_revision_id: inventoryRevisionId } : {}),
          },
          editLockToken);
        // 草稿这边只跟着改连带变化的**来源事实**（病害名称与标度），而且必须等服务端
        // 写成之后再改。反过来（先 dispatch 后调接口）有两处坏处：
        //
        //   编辑锁失效、版本冲突或网络失败时，关系表没写，本地却已经把病害名称改成了
        //   节点名——错误提示看得见，脏草稿看不见，而用户还能把它保存进去。
        //
        //   即便接口成功，服务端算 resolved_match_input_hash 用的是**数据库里**的来源
        //   事实，那时 defect_type 还是旧的（它进哈希，见 ResolutionHashes）。随后保存
        //   新名称，重算出的 match_input_hash 与裁决时的不等，界面就报"人工裁决后内容
        //   发生变化，请复核"——而那个变化正是这次选择自己造成的。
        dispatch({
          type: "select_rating_tree_node",
          candidateId: defect.candidate_id,
          versionId: ratingTreeVersionId,
          nodeId: selected.id,
          nodeName: selected.display_name,
          isScoring: selected.is_scoring,
          matchEvidence: "用户在精细维护中从当前构件适用节点选择",
        });
        onRatingResolved?.();
      } catch (caught) {
        setRatingError(caught instanceof ApiError
          ? caught.message
          : "评定树选择保存失败，请刷新后重试。");
      }
    })();
  };

  useEffect(() => {
    if (!ratingTreeVersionId || !row.resolution.ratingTreeNodeId) {
      setTreeNode(null);
      return;
    }
    if (row.ratingTreeNode?.id === row.resolution.ratingTreeNodeId) {
      setTreeNode(row.ratingTreeNode);
      return;
    }
    let active = true;
    setTreeNode(null);
    setTreeNodeError("");
    void fetchRatingTreeNode(baseUrl, ratingTreeVersionId, row.resolution.ratingTreeNodeId)
      .then((node) => {
        if (active) setTreeNode(node);
      })
      .catch((error) => {
        if (active) setTreeNodeError(ratingTreeErrorMessage(error));
      });
    return () => {
      active = false;
    };
  }, [baseUrl, row.resolution.ratingTreeNodeId, ratingTreeVersionId, row.ratingTreeNode]);

  useEffect(() => {
    let active = true;
    setComponentArchive(null);
    setHistoryUnavailable(false);
    if (!bridgeId || !boundComponentId) {
      setHistoryLoading(false);
      return () => { active = false; };
    }
    setHistoryLoading(true);
    void fetchComponentArchive(baseUrl, boundComponentId)
      .then((archive) => {
        if (active) setComponentArchive(archive);
      })
      .catch(() => {
        if (active) setHistoryUnavailable(true);
      })
      .finally(() => {
        if (active) setHistoryLoading(false);
      });
    return () => { active = false; };
  }, [baseUrl, boundComponentId, bridgeId]);

  const location = displayDefectLocation(defect.defect_location);
  const confirmed = defect.group_review_status === "已确认";
  const ignored = defect.review_status === "已忽略";
  const statusLabel = ignored ? "已忽略" : confirmed ? "已确认" : "待确认";
  const statusColor = ignored ? "default" : confirmed ? "success" : "warning";
  const sourceLabel = [
    defect.source_ref.table_title,
    defect.source_ref.row_index !== null && defect.source_ref.row_index !== undefined
      ? `第 ${defect.source_ref.row_index} 行` : null,
  ].filter(Boolean).join(" ");
  const measurementText = measurementSummary
    ? `${measurementSummary}${defect.measurements.length === 0 ? "（未能解析，按原文入库）" : ""}`
    : "—";
  const scaleDescription = currentScale !== null ? selectedNodeScaleDescriptions[String(currentScale)] : undefined;

  // 历年演变压成一行：通常只有"当前"一条，原来那张大卡片大半是空白。
  const history = (
    <Card
      size="small"
      aria-label="历年病害演变"
      title="历年病害演变"
      extra={archiveHref ? (
        <Typography.Link href={archiveHref} target="_blank" rel="noreferrer">查看构件完整病害档案 ›</Typography.Link>
      ) : null}
      styles={{ body: { paddingBlock: 10 } }}
    >
      <Flex vertical gap={6}>
        <Flex align="center" gap={8} wrap>
          <Tag color="processing" variant="filled">{draft.inspection.inspection_year} 当前</Tag>
          <Typography.Text>
            {currentScale !== null ? `标度 ${currentScale}` : "标度待确认"}
            {measurementSummary ? ` · ${measurementSummary}` : ""}
          </Typography.Text>
        </Flex>
        {historicalObservations.map((observation, index) => (
          <Flex key={observation.id} align="center" gap={8} wrap>
            {/* 最近的一次叫「上次检测」，再往前只能说是历史记录。 */}
            <Tag variant="filled">{observation.inspection_year} {index === 0 ? "上次检测" : "历史记录"}</Tag>
            <Typography.Text type="secondary">
              {observation.scale ? `标度 ${observation.scale}` : "无标度"}
              {observation.measurements.length > 0
                ? ` · ${observation.measurements.map((measurement) => measurement.raw_text).filter(Boolean).join("、")}`
                : ""}
            </Typography.Text>
          </Flex>
        ))}
        {historyLoading ? <Typography.Text type="secondary">正在读取历年记录…</Typography.Text> : null}
        {!historyLoading && historyUnavailable ? (
          <Typography.Text type="secondary">历年记录暂不可用，可前往完整档案查看。</Typography.Text>
        ) : null}
        {!historyLoading && !historyUnavailable && historicalObservations.length === 0 ? (
          <Typography.Text type="secondary">暂无同构件、同类病害的往年记录。</Typography.Text>
        ) : null}
        {historicalObservations.length > 0 ? (
          <Typography.Text type="warning"><span aria-hidden="true">⚠ </span>{scaleTrend}</Typography.Text>
        ) : null}
      </Flex>
    </Card>
  );

  return (
    <Flex role="group" aria-label="病害详情维护" wrap style={{ minHeight: 0 }}>
      {/* 维护栏：头部 → 表单 → 历年演变 → 固定操作条。 */}
      <Flex vertical style={{ flex: "1 1 420px", minWidth: 0 }}>
        <Flex
          align="center"
          gap={12}
          wrap
          style={{ paddingBottom: 12, borderBottom: `1px solid ${token.colorSplit}` }}
        >
          {/* 面板名是恒定的，每次打开都一样；真正要一眼确认的是"这是哪条病害"。 */}
          <Flex vertical style={{ minWidth: 0 }}>
            <Flex align="baseline" gap={8} wrap>
              <Typography.Title level={5} style={{ margin: 0 }}>
                {defect.component_number ?? defect.component_name}
              </Typography.Title>
              {location ? <Typography.Text type="secondary">{location}</Typography.Text> : null}
              <Tag color={statusColor} variant="filled">{statusLabel}</Tag>
            </Flex>
            {sourceLabel ? (
              <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>来源 {sourceLabel}</Typography.Text>
            ) : null}
          </Flex>

          <Flex align="center" gap={6} style={{ marginInlineStart: "auto" }}>
            {/* 三项检查回答「这条现在能不能确认」：缩成三枚标签贴在标题行，悬停看全称。 */}
            <Flex gap={4} role="group" aria-label={`校对完整度 ${completeness}%`}>
              {completenessItems.map((item) => (
                <Tooltip key={item.label} title={item.label}>
                  <Tag
                    color={item.complete ? "success" : "warning"}
                    variant="filled"
                    icon={item.complete ? <CheckCircleFilled /> : <ExclamationCircleFilled />}
                    style={{ marginInlineEnd: 0 }}
                  >
                    {item.label.slice(0, 2)}
                  </Tag>
                </Tooltip>
              ))}
            </Flex>
            <Divider orientation="vertical" />
            <Tooltip title="上一条">
              <Button aria-label="上一条" icon={<UpOutlined />} disabled={!onPrevious} onClick={onPrevious} />
            </Tooltip>
            <Tooltip title="下一条">
              <Button aria-label="下一条" icon={<DownOutlined />} disabled={!onNext} onClick={onNext} />
            </Tooltip>
            {/* 关闭挂在维护区的外边界上，放在照片栏旁会被误当成「关掉照片」。 */}
            <Tooltip title="收起精细维护">
              <Button type="text" aria-label="关闭精细维护" icon={<CloseOutlined />} onClick={onClose} />
            </Tooltip>
          </Flex>
        </Flex>

        <Flex vertical gap={14} style={{ paddingBlock: 14, flex: 1, minHeight: 0 }}>
          {row.problems.length > 0 ? (
            <Alert
              type="warning"
              showIcon
              role="note"
              title={row.problems.length === 1 ? row.problems[0].message : `还有 ${row.problems.length} 项待处理`}
              description={row.problems.length > 1 ? (
                <Flex vertical gap={2}>
                  {row.problems.map((problem) => <span key={problem.code}>{problem.message}</span>)}
                </Flex>
              ) : undefined}
            />
          ) : null}
          {treeNodeError ? <Alert type="error" showIcon role="alert" title={treeNodeError} /> : null}
          {/* 节点没存进解析表时必须说出来：界面显示成选上了、刷新后却没有，比直接报错更难查。 */}
          {ratingError ? <Alert type="error" showIcon role="alert" title={ratingError} /> : null}

          {disabled ? (
            // 只读时不摆一屏灰掉的输入框：看的是结果，用描述列表。
            <Descriptions
              size="small"
              bordered
              column={2}
              styles={{ label: { width: 96, whiteSpace: "nowrap" } }}
              items={[
                { key: "node", label: "评定树病害", span: 2, children: resolvedNodeLabel || "尚未确定规范病害" },
                { key: "location", label: "病害位置", children: defect.defect_location || "—" },
                {
                  key: "scale",
                  label: "幅度",
                  children: (
                    <Flex vertical>
                      <span>{currentScale !== null ? `${currentScale} 级` : "—"}</span>
                      {scaleDescription ? (
                        <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>{scaleDescription}</Typography.Text>
                      ) : null}
                    </Flex>
                  ),
                },
                { key: "description", label: "病害描述", span: 2, children: defect.defect_description || "—" },
                { key: "measurements", label: "尺寸与数量", span: 2, children: measurementText },
              ]}
            />
          ) : (
            <Form layout="vertical" style={{ marginBottom: 0 }}>
              <Form.Item
                label="评定树病害"
                htmlFor="defect-rating-node"
                style={{ marginBottom: 12 }}
                extra={
                  // 已经定下来的只给原文；还没定时依据才是选择所必需的信息。
                  !row.resolution.ratingTreeNodeId && evidence && row.matchState !== "composite"
                    ? evidence
                    : defect.defect_type ? `原文：${defect.defect_type}` : undefined
                }
              >
                {/* 下拉本身就是当前值的显示，也是唯一的赋值入口（无匹配的病害没有候选可点），所以常驻。 */}
                <Select
                  id="defect-rating-node"
                  aria-label="评定树病害"
                  title={resolvedNodeLabel || undefined}
                  disabled={!ratingTreeVersionId}
                  value={row.resolution.ratingTreeNodeId ?? ""}
                  onChange={(value: string) => selectNode(value)}
                  options={[
                    { value: "", label: "请选择评定树病害" },
                    ...orderedApplicableNodes.map((item) => ({
                      value: item.id,
                      label: `${ratingTreeOptionLabel(item, orderedApplicableNodes)}${item.is_scoring ? "" : "（暂不计分）"}`,
                    })),
                  ]}
                />
              </Form.Item>

              {row.matchState === "composite" ? (
                <Alert
                  type="warning"
                  showIcon
                  role="note"
                  style={{ marginBottom: 12 }}
                  title="这条记录同时命中多个规范病害。请改写描述拆成单一病害，或在下方候选中确认为其中一个。"
                />
              ) : null}
              {row.matchCandidates.length > 0 ? (
                <Flex vertical gap={6} style={{ marginBottom: 12 }}>
                  {row.matchCandidates.map((candidate) => (
                    <Flex
                      key={candidate.rating_tree_node_id}
                      align="center"
                      justify="space-between"
                      gap={10}
                      style={{ padding: "6px 10px", borderRadius: token.borderRadius, background: token.colorFillQuaternary }}
                    >
                      <Flex vertical style={{ minWidth: 0 }}>
                        <Typography.Text strong>{candidate.display_name}</Typography.Text>
                        <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                          {matchMethodLabel(candidate.match_method)}
                          {displayMatchEvidence(candidate.evidence, candidate.match_method)
                            ? ` · ${displayMatchEvidence(candidate.evidence, candidate.match_method)}`
                            : ""}
                        </Typography.Text>
                      </Flex>
                      <Button size="small" disabled={!ratingTreeVersionId} onClick={() => selectNode(candidate.rating_tree_node_id)}>
                        采用
                      </Button>
                    </Flex>
                  ))}
                </Flex>
              ) : null}

              <Flex gap={16} wrap>
                <Form.Item label="病害位置" htmlFor="defect-location" style={{ flex: "1 1 220px", marginBottom: 12 }}>
                  <Input
                    id="defect-location"
                    value={defect.defect_location}
                    onChange={(event) => dispatch({
                      type: "edit_defect_field",
                      candidateId: defect.candidate_id,
                      field: "defect_location",
                      value: event.target.value,
                    })}
                    onBlur={() => onDefectTextCommitted?.(defect.candidate_id)}
                  />
                </Form.Item>
                {/* 只列出该节点允许的取值；规范判定文字写在下方，不按描述反推。 */}
                <Form.Item
                  label="幅度"
                  htmlFor="defect-scale"
                  style={{ flex: "1 1 220px", marginBottom: 12 }}
                  extra={scaleDescription}
                >
                  <Select
                    id="defect-scale"
                    aria-label="幅度"
                    disabled={!selectedNodeIsScoring}
                    value={defect.defect_scale ?? ""}
                    onChange={(value: string | number) => dispatch({
                      type: "edit_defect_field",
                      candidateId: defect.candidate_id,
                      field: "defect_scale",
                      value: value === "" ? null : Number(value),
                    })}
                    options={[
                      {
                        value: "",
                        label: row.resolution.ratingTreeNodeId
                          ? (selectedNodeIsScoring ? "请选择幅度" : "该节点暂不计分")
                          : "请先确定规范病害",
                      },
                      ...selectedNodeAllowedScales.map((scale) => ({
                        value: scale,
                        label: `${scale} · ${selectedNodeScaleDescriptions[String(scale)] ?? ""}`,
                      })),
                    ]}
                  />
                </Form.Item>
              </Flex>

              <Form.Item label="病害描述" htmlFor="defect-description" style={{ marginBottom: 12 }}>
                <Input.TextArea
                  id="defect-description"
                  autoSize={{ minRows: 2, maxRows: 4 }}
                  value={defect.defect_description}
                  onChange={(event) => dispatch({
                    type: "edit_defect_field",
                    candidateId: defect.candidate_id,
                    field: "defect_description",
                    value: event.target.value,
                  })}
                  onBlur={() => onDefectTextCommitted?.(defect.candidate_id)}
                />
              </Form.Item>

              {/* 尺寸原文只喂解析器，展示的是解析结果；解析不出来时原文才是唯一记录。 */}
              <Form.Item label="尺寸与数量" extra="由病害描述自动解析，改描述后重新解析" style={{ marginBottom: 0 }}>
                <Typography.Text aria-label="尺寸与数量">{measurementText}</Typography.Text>
              </Form.Item>
            </Form>
          )}

          {history}
        </Flex>

        {disabled ? null : (
          <Flex
            align="center"
            gap={8}
            wrap
            style={{ paddingTop: 12, borderTop: `1px solid ${token.colorSplit}` }}
          >
            {ignored ? (
              <Button
                type="text"
                aria-label="恢复病害"
                icon={<UndoOutlined />}
                onClick={() => dispatch({ type: "restore_ignored_defect", candidateId: defect.candidate_id })}
              >
                恢复病害
              </Button>
            ) : (
              <Button
                type="text"
                danger
                aria-label="忽略此条"
                icon={<StopOutlined />}
                onClick={() => {
                  if (window.confirm("确定忽略这条病害？")) {
                    dispatch({ type: "ignore_defect", candidateId: defect.candidate_id });
                  }
                }}
              >
                忽略此条
              </Button>
            )}
            <Flex gap={8} style={{ marginInlineStart: "auto" }}>
              <Button disabled={!onSave} onClick={onSave}>保存修改</Button>
              {/* 确认后停在原地，不自动跳下一条：要看下一条由人自己点上面的箭头。 */}
              <Button type="primary" disabled={!row.confirmEligible} onClick={onConfirm}>确认</Button>
            </Flex>
          </Flex>
        )}
      </Flex>

      {/* 照片栏定宽；维护区被拖窄到放不下时整栏换到下方。 */}
      <div style={{ flex: "0 0 360px", maxWidth: "100%", paddingInlineStart: 16, borderInlineStart: `1px solid ${token.colorSplit}` }}>
        <DefectPhotoPanel
          draft={draft}
          defect={defect}
          cards={row.photoCards}
          importRecordId={importRecordId}
          baseUrl={baseUrl}
          initialPhotoCandidateId={initialPhotoCandidateId}
          dispatch={dispatch}
          disabled={disabled}
          editLockToken={editLockToken}
          allowUpload={allowDelete}
        />
      </div>
    </Flex>
  );
}
