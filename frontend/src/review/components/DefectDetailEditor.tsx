import { useEffect, useState, type Dispatch } from "react";

import type { ComponentInventoryRevision } from "../../api/componentInventoryApi";
import {
  fetchRatingTreeNode,
  ratingTreeErrorMessage,
  type RatingTreeNode,
  type RatingTreeNodeSummary,
} from "../../api/ratingTreeApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import { matchMethodLabel, type DefectReviewRow } from "../defectPhotoReviewModel";
import type { ReviewDraftAction } from "../reviewDraft";
import { ComponentMatchField } from "./ComponentMatchField";
import { displayDefectLocation } from "./displayHelpers";
import { DefectPhotoPanel } from "./DefectPhotoPanel";

interface DefectDetailEditorProps {
  draft: BridgeAnnualInspectionData;
  row: DefectReviewRow;
  ratingTreeVersionId: string | null;
  applicableNodes: RatingTreeNodeSummary[];
  componentInventory?: ComponentInventoryRevision | null;
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
}

export function DefectDetailEditor({
  draft,
  row,
  ratingTreeVersionId,
  applicableNodes,
  componentInventory,
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
}: DefectDetailEditorProps) {
  const defect = row.defect;
  const [treeNode, setTreeNode] = useState<RatingTreeNode | null>(row.ratingTreeNode);
  const [treeNodeError, setTreeNodeError] = useState("");
  // 候选是后端这次算出来的临时结果，不在草稿里；采用候选后按人工选择处理。
  const evidence =
    defect.rating_tree_match_evidence ?? row.matchResult?.reason_message ?? "";
  const selectNode = (nodeId: string) => {
    const selected = applicableNodes.find((item) => item.id === nodeId);
    if (!selected || !ratingTreeVersionId) return;
    dispatch({
      type: "select_rating_tree_node",
      candidateId: defect.candidate_id,
      versionId: ratingTreeVersionId,
      nodeId: selected.id,
      nodeName: selected.display_name,
      isScoring: selected.is_scoring,
      matchEvidence: "用户在精细维护中从当前构件适用节点选择",
    });
  };

  useEffect(() => {
    if (!ratingTreeVersionId || !defect.rating_tree_node_id) {
      setTreeNode(null);
      return;
    }
    if (row.ratingTreeNode?.id === defect.rating_tree_node_id) {
      setTreeNode(row.ratingTreeNode);
      return;
    }
    let active = true;
    setTreeNodeError("");
    void fetchRatingTreeNode(baseUrl, ratingTreeVersionId, defect.rating_tree_node_id)
      .then((node) => {
        if (active) setTreeNode(node);
      })
      .catch((error) => {
        if (active) setTreeNodeError(ratingTreeErrorMessage(error));
      });
    return () => {
      active = false;
    };
  }, [baseUrl, defect.rating_tree_node_id, ratingTreeVersionId, row.ratingTreeNode]);

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
      {row.problems.length > 0 ? (
        <div className="defect-detail-problems">
          {row.problems.map((problem) => <span key={problem.code}>{problem.message}</span>)}
        </div>
      ) : null}
      <div className="defect-detail-fields">
        <label>实际构件<ComponentMatchField defect={defect} inventory={componentInventory ?? null} /></label>
        <div className="defect-detail-wide defect-rating-tree-result" aria-label="评定树病害">
          <div className="defect-rating-tree-result-head">
            <span>评定树病害</span>
            <strong>{treeNode?.display_name ?? "尚未确定规范病害"}</strong>
            <em className={`defect-match-method ${row.matchState}`}>
              {defect.rating_tree_node_id
                ? matchMethodLabel(defect.rating_tree_match_method)
                : row.matchLabel}
            </em>
          </div>
          {/* 匹配依据必须写清命中的原文、别名或关键词，以及构件适用理由：
              用户要能不打开评定树就判断这条自动结果该不该认。 */}
          {evidence ? <p className="defect-match-evidence">{evidence}</p> : null}
          {row.matchState === "composite" ? (
            <p className="warning-text">
              这条记录同时命中多个规范病害。请改写描述拆成单一病害，或在下方候选中确认为其中一个。
            </p>
          ) : null}
          {row.matchCandidates.length > 0 ? (
            <ul className="defect-match-candidates">
              {row.matchCandidates.map((candidate) => (
                <li key={candidate.rating_tree_node_id}>
                  <button
                    type="button"
                    disabled={disabled || !ratingTreeVersionId}
                    onClick={() => selectNode(candidate.rating_tree_node_id)}
                  >
                    采用「{candidate.display_name}」
                  </button>
                  <small>{matchMethodLabel(candidate.match_method)} · {candidate.evidence}</small>
                </li>
              ))}
            </ul>
          ) : null}
          <details open={!defect.rating_tree_node_id}>
            <summary>展开完整评定树人工选择</summary>
            <select
              aria-label="评定树病害"
              disabled={disabled || !ratingTreeVersionId}
              value={defect.rating_tree_node_id ?? ""}
              onChange={(event) => selectNode(event.target.value)}
            >
              <option value="">请选择评定树病害</option>
              {applicableNodes.map((item) => (
                <option key={item.id} value={item.id}>
                  {item.display_name}{item.is_scoring ? "" : "（暂不计分）"}
                </option>
              ))}
            </select>
          </details>
        </div>
        <label>位置<input disabled={disabled} value={defect.defect_location} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_location", value: event.target.value })} onBlur={() => onDefectTextCommitted?.(defect.candidate_id)} /></label>
        <label>
          标度
          {/* 标度只列出该节点允许的取值并附完整规范判定文字；不按描述推断标度。 */}
          <select
            disabled={disabled || !treeNode?.is_scoring}
            value={defect.defect_scale ?? ""}
            onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_scale", value: event.target.value === "" ? null : Number(event.target.value) })}
          >
            <option value="">{treeNode ? (treeNode.is_scoring ? "请选择标度" : "该节点暂不计分") : "请先确定规范病害"}</option>
            {treeNode?.allowed_scales.map((scale) => (
              <option key={scale} value={scale}>
                {scale} · {treeNode.scale_descriptions[String(scale)] ?? ""}
              </option>
            ))}
          </select>
        </label>
        <label className="defect-detail-wide">病害描述<input disabled={disabled} value={defect.defect_description} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_description", value: event.target.value })} onBlur={() => onDefectTextCommitted?.(defect.candidate_id)} /></label>
      </div>
      {treeNodeError ? <p className="form-error" role="alert">{treeNodeError}</p> : null}
      {treeNode ? (
        <div className="defect-rating-tree-context">
          <div>
            <span>评定树路径</span>
            <strong>{treeNode.path.map((item) => item.display_name).join(" / ")}</strong>
          </div>
          <div>
            <span>评分规则</span>
            <strong>{treeNode.is_scoring ? `继承 H21 · ${treeNode.h21_indicator_name ?? treeNode.h21_indicator_id}` : "暂不计分"}</strong>
          </div>
          <a
            href={`/rating-trees/${encodeURIComponent(ratingTreeVersionId!)}?node=${encodeURIComponent(treeNode.id)}`}
            target="_blank"
            rel="noreferrer"
          >
            在评定树中查看
          </a>
        </div>
      ) : null}
      <details>
        <summary>检测量与补充信息</summary>
        <div className="defect-detail-fields">
          <label>数量<input disabled={disabled} value={defect.quantity_text ?? ""} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "quantity_text", value: event.target.value || null })} /></label>
          <label>尺寸原文<input disabled={disabled} value={defect.measurement_text ?? ""} onChange={(event) => dispatch({ type: "edit_measurement_text", candidateId: defect.candidate_id, text: event.target.value || null })} /></label>
          <label className="defect-detail-wide">校对备注<input disabled={disabled} value={defect.review_note ?? ""} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "review_note", value: event.target.value || null })} /></label>
        </div>
      </details>
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
        <button type="button" className="review-action-primary" disabled={disabled || !row.batchEligible} onClick={onConfirm}>确认本组</button>
      </div>
    </section>
  );
}
