import { useEffect, useMemo, useState, type Dispatch } from "react";

import {
  fetchRatingTreeNode,
  ratingTreeErrorMessage,
  type RatingTreeNode,
  type RatingTreeNodeSummary,
} from "../../api/ratingTreeApi";
import { ApiError } from "../../api/apiClient";
import { applyRatingResolution } from "../../api/resolutionApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import {
  ratingTreeDisplayLabel,
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
  initialPhotoCandidateId?: string | null;
  dispatch: Dispatch<ReviewDraftAction>;
  disabled?: boolean;
  allowDelete?: boolean;
  editLockToken?: string | null;
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
  initialPhotoCandidateId,
  dispatch,
  disabled = false,
  allowDelete = false,
  editLockToken,
  onConfirm,
  onClose,
  onDefectTextCommitted,
  onRatingResolved,
  inventoryRevisionId,
}: DefectDetailEditorProps) {
  const defect = row.defect;
  const [treeNode, setTreeNode] = useState<RatingTreeNode | null>(row.ratingTreeNode);
  const [treeNodeError, setTreeNodeError] = useState("");
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

  const selectNode = (nodeId: string) => {
    const selected = applicableNodes.find((item) => item.id === nodeId);
    if (!selected || !ratingTreeVersionId) return;

    // 草稿这边只跟着改连带变化的**来源事实**（病害名称与标度）。
    dispatch({
      type: "select_rating_tree_node",
      candidateId: defect.candidate_id,
      versionId: ratingTreeVersionId,
      nodeId: selected.id,
      nodeName: selected.display_name,
      isScoring: selected.is_scoring,
      matchEvidence: "用户在精细维护中从当前构件适用节点选择",
    });

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
        // 展开成多个构件的病害逐条写：只写第一条的话，其余仍按自动结果走。
        for (const instance of instances) {
          await applyRatingResolution(
            baseUrl,
            importRecordId,
            instance.instanceId,
            {
              expected_version: instance.ratingVersion,
              rating_tree_node_id: selected.id,
              expected_rating_tree_version_id: ratingTreeVersionId,
              // 台账版本变了，"这个节点适不适用于该构件"的答案就可能变；不带上它，
              // 旧页面能把一个基于过时映射的判断写进去。
              ...(inventoryRevisionId ? { expected_inventory_revision_id: inventoryRevisionId } : {}),
            },
            editLockToken);
        }
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

  return (
    <section className="defect-detail-editor" aria-label="病害详情维护">
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
        <div className="defect-detail-heading-actions">
          <span className={`defect-quick-status ${row.status}`}>
            {defect.review_status}{defect.group_review_status === "已确认" ? " · 本组已确认" : ""}
          </span>
          <button type="button" className="defect-detail-close" aria-label="关闭精细维护" onClick={onClose}>×</button>
        </div>
      </div>
      {/* 判定与照片并排：这一步的动作就是"看图判断这条该不该是这个评定树节点"，
          两者不同屏就得来回滚。窄到放不下两列时由容器查询退回单列。 */}
      <div className="defect-detail-body">
        <div className="defect-detail-primary">
          {row.problems.length > 0 ? (
            <div className="defect-detail-problems">
              {row.problems.map((problem) => <span key={problem.code}>{problem.message}</span>)}
            </div>
          ) : null}
          <div className="defect-rating-tree-result" aria-label="评定树病害">
            <div className="defect-rating-tree-result-head">
              <span>评定树病害</span>
              {/* 定了病害就不在这里重复写名字——下面的下拉显示的就是它。
                  没定时这句红字必须留着：下拉那时只是一片空白，说不出"还没定"。 */}
              {row.resolution.ratingTreeNodeId ? null : <strong>尚未确定规范病害</strong>}
              <em className={`defect-match-method ${row.matchState}`}>
                {row.resolution.ratingTreeNodeId
                  ? matchMethodLabel(row.resolution.ratingMatchMethod)
                  : row.matchLabel}
              </em>
            </div>
            {/* 匹配依据必须写清命中的原文、别名或关键词，以及构件适用理由：
                用户要能不打开评定树就判断这条自动结果该不该认。 */}
            {/* 组合病害时后端的 reason_message 与下面那句说的是同一件事，
                只留带行动指引的那句，不把同一句话在同一屏说两遍。 */}
            {evidence && row.matchState !== "composite" ? (
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
            {/* 路径和评分规则说的就是上面这个下拉选中的节点，原先却独占一块带边框的
                灰条摆在字段区下方；并进这张卡，读的时候不用在两块之间来回对。 */}
            {activeTreeNode ? (
              <div className="defect-rating-tree-context">
                <div>
                  <span>评定树路径</span>
                  <strong>{activeTreeNode.path.map((item) => ratingTreeDisplayLabel(item)).join(" / ")}</strong>
                </div>
                <div>
                  <span>评分规则</span>
                  <strong>{activeTreeNode.is_scoring ? `继承 H21 · ${activeTreeNode.h21_indicator_name ?? activeTreeNode.h21_indicator_id}` : "暂不计分"}</strong>
                </div>
                <a
                  href={`/rating-trees/${encodeURIComponent(ratingTreeVersionId!)}?node=${encodeURIComponent(activeTreeNode.id)}`}
                  target="_blank"
                  rel="noreferrer"
                >
                  在评定树中查看
                </a>
              </div>
            ) : null}
          </div>
          {treeNodeError ? <p className="form-error" role="alert">{treeNodeError}</p> : null}
          {/* 节点没存进解析表时必须说出来：界面显示成选上了、刷新后却没有，比直接报错更难查。 */}
          {ratingError ? <p className="form-error" role="alert">{ratingError}</p> : null}
          <div className="defect-detail-fields">
            <label>位置<input disabled={disabled} value={defect.defect_location} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_location", value: event.target.value })} onBlur={() => onDefectTextCommitted?.(defect.candidate_id)} /></label>
            <label>
              标度
              {/* 标度只列出该节点允许的取值并附完整规范判定文字；不按描述推断标度。 */}
              <select
                disabled={disabled || !selectedNodeIsScoring}
                value={defect.defect_scale ?? ""}
                onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_scale", value: event.target.value === "" ? null : Number(event.target.value) })}
              >
                <option value="">{row.resolution.ratingTreeNodeId ? (selectedNodeIsScoring ? "请选择标度" : "该节点暂不计分") : "请先确定规范病害"}</option>
                {selectedNodeAllowedScales.map((scale) => (
                  <option key={scale} value={scale}>
                    {scale} · {selectedNodeScaleDescriptions[String(scale)] ?? ""}
                  </option>
                ))}
              </select>
            </label>
            <label className="defect-detail-wide">
              病害描述
              {/* 描述由位置和病害类型拼成，尺寸另有一列；尺寸原文只喂解析器，
                  不再单独给一个框——那是"同一句话出现两遍"的来源。 */}
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
            {measurementSummary ? (
              <p className="defect-detail-wide defect-measurement-summary">
                尺寸：{measurementSummary}
                {defect.measurements.length === 0 ? "（未能解析，按原文入库）" : ""}
              </p>
            ) : null}
          </div>
        </div>
        <div className="defect-detail-photo-column">
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
      </div>
      <div className="defect-detail-actions">
        {defect.review_status === "已忽略" ? (
          <button type="button" disabled={disabled} onClick={() => dispatch({ type: "restore_ignored_defect", candidateId: defect.candidate_id })}>恢复病害</button>
        ) : (
          <button type="button" disabled={disabled} onClick={() => {
            if (window.confirm("确定忽略这条病害？")) dispatch({ type: "ignore_defect", candidateId: defect.candidate_id });
          }}>忽略病害</button>
        )}
        {/* 删除不可逆（照片会退回未关联区），与"忽略"用同一副长相太容易点错。 */}
        {allowDelete ? <button type="button" className="danger-text-button" disabled={disabled} onClick={() => {
          if (window.confirm("确定删除这条病害？已关联照片会回到未关联照片区。")) {
            dispatch({ type: "delete_defect", candidateId: defect.candidate_id });
          }
        }}>删除病害</button> : null}
        <button type="button" className="review-action-primary" disabled={disabled || !row.confirmEligible} onClick={onConfirm}>确认本组</button>
      </div>
    </section>
  );
}
