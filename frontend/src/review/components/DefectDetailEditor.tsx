import { useEffect, useMemo, useState, type Dispatch } from "react";

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

  return (
    <section className="defect-detail-editor" aria-label="病害详情维护">
      {/* 维护栏与照片栏是并排的两栏，各自从内容区顶端起排。照片栏原先嵌在
          .defect-detail-body 里，被上面的标题区整块压低了一截——那是「照片与证据」
          比另外两栏的栏名低一大截的原因，所以提出来做同级列。 */}
      <div className="defect-detail-main">
        <div className="defect-detail-heading">
          <div className="defect-detail-identity">
            {/* 面板名是恒定的，每次打开都一样；真正要一眼确认的是"这是哪条病害"。
                所以标题给病害编号，面板名降成上方的小字说明。 */}
            <p className="defect-detail-kicker">精细维护病害档案</p>
            <h3>
              {defect.component_number ?? defect.component_name}
              {displayDefectLocation(defect.defect_location) ? (
                <span className="defect-detail-location">{displayDefectLocation(defect.defect_location)}</span>
              ) : null}
            </h3>
          </div>
        </div>
      <div className="defect-detail-body">
        <div className="defect-detail-primary">
          {row.problems.length > 0 ? (
            <div className="defect-detail-problems">
              {row.problems.map((problem) => <span key={problem.code}>{problem.message}</span>)}
            </div>
          ) : null}
          <div className={`defect-rating-tree-result ${row.resolution.ratingTreeNodeId ? "resolved" : ""}`} aria-label="病害类型">
            <div className="defect-rating-tree-result-head">
              <span>病害类型</span>
              {row.resolution.ratingTreeNodeId ? null : <strong>尚未确定规范病害</strong>}
            </div>
            {/* 已经确定的病害只显示结果；匹配来源与命中证据对日常校对没有帮助。
                只有尚未决策时，依据才是选择候选所必需的信息。 */}
            {!row.resolution.ratingTreeNodeId && evidence && row.matchState !== "composite" ? (
              <p className="defect-match-evidence">{evidence}</p>
            ) : null}
            {row.matchState === "composite" ? (
              <p className="warning-text">
                这条记录同时命中多个规范病害。请改写描述拆成单一病害，或在下方候选中确认为其中一个。
              </p>
            ) : null}
            {row.matchCandidates.length > 0 ? (
              <ul className="defect-match-candidates">
                {row.matchCandidates.map((candidate) => (
                  <li key={candidate.rating_tree_node_id}>
                    <div className="defect-match-candidate-text">
                      <strong>{candidate.display_name}</strong>
                      <small>
                        {matchMethodLabel(candidate.match_method)}
                        {displayMatchEvidence(candidate.evidence, candidate.match_method)
                          ? ` · ${displayMatchEvidence(candidate.evidence, candidate.match_method)}`
                          : ""}
                      </small>
                    </div>
                    <button
                      type="button"
                      disabled={disabled || !ratingTreeVersionId}
                      onClick={() => selectNode(candidate.rating_tree_node_id)}
                    >
                      采用
                    </button>
                  </li>
                ))}
              </ul>
            ) : null}
            {/* 下拉本身就是当前值的显示，同时也是唯一的赋值入口
                （无匹配的病害没有候选可点），所以常驻，不藏在折叠或按钮后面。 */}
            <select
              aria-label="评定树病害"
              /* 窄栏里长标签会省略成 …，完整文案交给悬停。 */
              title={resolvedNodeLabel || undefined}
              disabled={disabled || !ratingTreeVersionId}
              value={row.resolution.ratingTreeNodeId ?? ""}
              onChange={(event) => selectNode(event.target.value)}
            >
              <option value="">请选择评定树病害</option>
              {orderedApplicableNodes.map((item) => (
                <option key={item.id} value={item.id}>
                  {ratingTreeOptionLabel(item, orderedApplicableNodes)}{item.is_scoring ? "" : "（暂不计分）"}
                </option>
              ))}
            </select>
          </div>
          {treeNodeError ? <p className="form-error" role="alert">{treeNodeError}</p> : null}
          {/* 节点没存进解析表时必须说出来：界面显示成选上了、刷新后却没有，比直接报错更难查。 */}
          {ratingError ? <p className="form-error" role="alert">{ratingError}</p> : null}
          {/* 两列各自成栏，不走 grid 自动流：左列录入链路（位置 → 类型 → 描述 → 尺寸），
              右列判定链路（幅度 → 判据 → 完整度）。两列条目数和行高都不等，交给自动流
              会让「病害描述」和「标度判定依据」错位。 */}
          <div className="defect-detail-fields">
            <div className="defect-detail-field-col">
              <label>病害位置<input disabled={disabled} value={defect.defect_location} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_location", value: event.target.value })} onBlur={() => onDefectTextCommitted?.(defect.candidate_id)} /></label>
              <label>病害类型<input readOnly title={resolvedNodeLabel || undefined} value={resolvedNodeLabel || "尚未确定规范病害"} /></label>
              <label>
                病害描述
                <input
                  disabled={disabled}
                  value={defect.defect_description}
                  onChange={(event) => dispatch({
                    type: "edit_defect_field",
                    candidateId: defect.candidate_id,
                    field: "defect_description",
                    value: event.target.value,
                  })}
                  onBlur={() => onDefectTextCommitted?.(defect.candidate_id)}
                />
              </label>
              <label>
                尺寸与数量
                {/* 尺寸原文只喂解析器，展示的是解析结果；解析不出来时原文才是唯一记录。 */}
                <input
                  readOnly
                  value={measurementSummary
                    ? `${measurementSummary}${defect.measurements.length === 0 ? "（未能解析，按原文入库）" : ""}`
                    : "—"}
                />
              </label>
            </div>
            <div className="defect-detail-field-col">
              <label>
                幅度
                {/* 只列出该节点允许的取值并附完整规范判定文字；不按描述反推。 */}
                <select
                  disabled={disabled || !selectedNodeIsScoring}
                  value={defect.defect_scale ?? ""}
                  onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_scale", value: event.target.value === "" ? null : Number(event.target.value) })}
                >
                  <option value="">{row.resolution.ratingTreeNodeId ? (selectedNodeIsScoring ? "请选择幅度" : "该节点暂不计分") : "请先确定规范病害"}</option>
                  {selectedNodeAllowedScales.map((scale) => (
                    <option key={scale} value={scale}>
                      {scale} · {selectedNodeScaleDescriptions[String(scale)] ?? ""}
                    </option>
                  ))}
                </select>
              </label>
              {/* 三项检查紧跟幅度：判定链路走到这里就该回答「这条现在能不能确认」。 */}
              <section className="defect-completeness" aria-label={`校对完整度 ${completeness}%`}>
                <div className="defect-completeness-head">
                  <strong>校对完整度</strong>
                  <span>{completeness}%</span>
                </div>
                <ul className="defect-detail-checks">
                  {completenessItems.map((item) => (
                    <li key={item.label} className={item.complete ? "complete" : "pending"}>
                      <span aria-hidden="true">{item.complete ? "✓" : "!"}</span>{item.label}
                    </li>
                  ))}
                </ul>
              </section>
            </div>
          </div>
        </div>
      </div>
      {/* 历年演变落在字段区与操作栏之间那片空白上：它是通栏的参考信息，不参与
          左右两条录入 / 判定链路，占满整幅比挤在右列窄栏里更好读。 */}
      <section className="defect-history-card" aria-label="历年病害演变">
        <div className="defect-history-heading">
          <span>历年病害演变</span>
        </div>
        <ol className="defect-history-list">
          <li className="current">
            <span className="defect-history-dot" aria-hidden="true" />
            <time>{draft.inspection.inspection_year}</time>
            <strong>当前</strong>
            <small>{currentScale !== null ? `标度 ${currentScale}` : "标度待确认"}{measurementSummary ? ` · ${measurementSummary}` : ""}</small>
          </li>
          {historicalObservations.map((observation, index) => (
            <li key={observation.id}>
              <span className="defect-history-dot" aria-hidden="true" />
              <time>{observation.inspection_year}</time>
              {/* 最近的一次叫「上次检测」，再往前只能说是历史记录——它跟"上一次"隔着年份。 */}
              <strong>{index === 0 ? "上次检测" : "历史记录"}</strong>
              <small>{observation.scale ? `标度 ${observation.scale}` : "无标度"}{observation.measurements.length > 0 ? ` · ${observation.measurements.map((measurement) => measurement.raw_text).filter(Boolean).join("、")}` : ""}</small>
            </li>
          ))}
        </ol>
        {historyLoading ? <p className="defect-history-empty">正在读取历年记录…</p> : null}
        {!historyLoading && historyUnavailable ? <p className="defect-history-empty">历年记录暂不可用，可前往完整档案查看。</p> : null}
        {!historyLoading && !historyUnavailable && historicalObservations.length === 0 ? (
          <p className="defect-history-empty">暂无同构件、同类病害的往年记录。</p>
        ) : null}
        {historicalObservations.length > 0 ? (
          <p className="defect-history-trend"><span aria-hidden="true">⚠</span>{scaleTrend}</p>
        ) : null}
        {archiveHref ? (
          <a className="defect-history-archive-link" href={archiveHref} target="_blank" rel="noreferrer">查看构件完整病害档案 ›</a>
        ) : null}
      </section>
      <div className="defect-detail-actions">
        {/* 三个按钮成组，整组一起换行，不会出现「两个在上、确认按钮独占一行」。 */}
        <div className="defect-detail-action-buttons">
          <button type="button" disabled={disabled || !onSave} onClick={onSave}>保存修改</button>
          {defect.review_status === "已忽略" ? (
            <button type="button" disabled={disabled} onClick={() => dispatch({ type: "restore_ignored_defect", candidateId: defect.candidate_id })}>恢复病害</button>
          ) : (
            <button type="button" disabled={disabled} onClick={() => {
              if (window.confirm("确定忽略这条病害？")) dispatch({ type: "ignore_defect", candidateId: defect.candidate_id });
            }}>忽略此条</button>
          )}
          <button type="button" className="review-action-primary" disabled={disabled || !row.confirmEligible} onClick={onConfirm}>确认</button>
        </div>
      </div>
      </div>
      <div className="defect-detail-photo-column">
        {/* 关闭整个维护区的入口挂在最右一栏的右上角——那是这块区域的外边界，
            放在中栏标题旁会被误当成「关掉照片」。 */}
        <button type="button" className="defect-detail-close" aria-label="关闭精细维护" onClick={onClose}>×</button>
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
    </section>
  );
}
