import { useEffect, useState, type Dispatch } from "react";

import type { ComponentInventoryRevision } from "../../api/componentInventoryApi";
import {
  fetchRatingTreeNode,
  ratingTreeErrorMessage,
  type RatingTreeNode,
  type RatingTreeNodeSummary,
} from "../../api/ratingTreeApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import type { DefectReviewRow } from "../defectPhotoReviewModel";
import type { ReviewDraftAction } from "../reviewDraft";
import { ComponentMatchField } from "./ComponentMatchField";
import { PhotoRelationEditor } from "./PhotoRelationEditor";

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
  onConfirm: () => void;
  onClose: () => void;
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
  onConfirm,
  onClose,
}: DefectDetailEditorProps) {
  const defect = row.defect;
  const [treeNode, setTreeNode] = useState<RatingTreeNode | null>(row.ratingTreeNode);
  const [treeNodeError, setTreeNodeError] = useState("");

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
        <div>
          <h3>精细维护病害档案</h3>
          <p>{defect.component_number ?? defect.component_name} · {defect.defect_location}</p>
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
        <label>
          评定树病害
          <select
            disabled={disabled || !ratingTreeVersionId}
            value={defect.rating_tree_node_id ?? ""}
            onChange={(event) => {
              const selected = applicableNodes.find((item) => item.id === event.target.value);
              if (selected) dispatch({
                type: "select_rating_tree_node",
                candidateId: defect.candidate_id,
                versionId: ratingTreeVersionId!,
                nodeId: selected.id,
                nodeName: selected.display_name,
                isScoring: selected.is_scoring,
                matchEvidence: "用户在精细维护中从当前构件适用节点选择",
              });
            }}
          >
            <option value="">请选择评定树病害</option>
            {applicableNodes.map((item) => (
              <option key={item.id} value={item.id}>
                {item.display_name}{item.is_scoring ? "" : "（暂不计分）"}
              </option>
            ))}
          </select>
        </label>
        <label>位置<input disabled={disabled} value={defect.defect_location} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_location", value: event.target.value })} /></label>
        <label>
          标度
          <select
            disabled={disabled || !treeNode?.is_scoring}
            value={defect.defect_scale ?? ""}
            onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_scale", value: event.target.value === "" ? null : Number(event.target.value) })}
          >
            <option value="">请选择标度</option>
            {treeNode?.allowed_scales.map((scale) => (
              <option key={scale} value={scale}>
                {scale} · {treeNode.scale_descriptions[String(scale)] ?? ""}
              </option>
            ))}
          </select>
        </label>
        <label className="defect-detail-wide">病害描述<input disabled={disabled} value={defect.defect_description} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_description", value: event.target.value })} /></label>
      </div>
      {treeNodeError ? <p className="form-error" role="alert">{treeNodeError}</p> : null}
      {treeNode ? (
        <div className="defect-rating-tree-context">
          <div>
            <span>评定树路径</span>
            <strong>{treeNode.path.map((item) => item.display_name).join(" / ")}</strong>
          </div>
          <div>
            <span>匹配方式</span>
            <strong>{defect.rating_tree_match_method === "controlled_alias" ? "受控别名" : defect.rating_tree_match_method === "exact" ? "精确匹配" : defect.rating_tree_match_method === "fuzzy_candidate" ? "模糊建议（待确认）" : "人工选择"}</strong>
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
      <details>
        <summary>Word 原始记录</summary>
        {defect.source_ref.source_type === "manual" ? (
          <p>人工新增，无 Word 来源。</p>
        ) : (
          <div className="word-source-evidence">
            <span>{defect.source_ref.table_title ?? "未命名表格"} · 第 {defect.source_ref.row_index ?? "?"} 行</span>
            <pre>{defect.source_ref.raw_row_text ?? "未保存原始行文本"}</pre>
          </div>
        )}
      </details>
      <PhotoRelationEditor
        draft={draft}
        defect={defect}
        importRecordId={importRecordId}
        baseUrl={baseUrl}
        initialPhotoCandidateId={initialPhotoCandidateId}
        dispatch={dispatch}
        disabled={disabled}
      />
      <div className="defect-detail-actions">
        {defect.review_status === "已忽略" ? (
          <button type="button" disabled={disabled} onClick={() => dispatch({ type: "restore_ignored_defect", candidateId: defect.candidate_id })}>恢复病害</button>
        ) : (
          <button type="button" disabled={disabled} onClick={() => {
            if (window.confirm("确定忽略这条病害？")) dispatch({ type: "ignore_defect", candidateId: defect.candidate_id });
          }}>忽略病害</button>
        )}
        {allowDelete ? <button type="button" disabled={disabled} onClick={() => {
          if (window.confirm("确定删除这条病害？已关联照片会回到未关联照片区。")) {
            dispatch({ type: "delete_defect", candidateId: defect.candidate_id });
          }
        }}>删除病害</button> : null}
        <button type="button" className="review-action-primary" disabled={disabled || !row.batchEligible} onClick={onConfirm}>确认本组</button>
      </div>
    </section>
  );
}
