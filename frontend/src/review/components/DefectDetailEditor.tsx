import type { Dispatch } from "react";

import type { StandardDefectCatalog, StandardDefectIndicator } from "../../api/standardsApi";
import type { ComponentInventoryRevision } from "../../api/componentInventoryApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import type { DefectReviewRow } from "../defectPhotoReviewModel";
import type { ReviewDraftAction } from "../reviewDraft";
import { ComponentMatchField } from "./ComponentMatchField";
import { PhotoRelationEditor } from "./PhotoRelationEditor";

interface DefectDetailEditorProps {
  draft: BridgeAnnualInspectionData;
  row: DefectReviewRow;
  catalogs: StandardDefectCatalog[];
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

function applicableIndicators(
  catalogs: StandardDefectCatalog[],
  categoryId: string | null | undefined,
): StandardDefectIndicator[] {
  if (!categoryId) return [];
  return catalogs
    .filter((catalog) => catalog.applicable_component_ids.includes(categoryId))
    .flatMap((catalog) => catalog.indicators);
}

export function DefectDetailEditor({
  draft,
  row,
  catalogs,
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
  const indicators = applicableIndicators(catalogs, defect.standard_component_category_id);
  const indicator = indicators.find((item) => item.id === defect.standard_defect_indicator_id) ?? null;

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
          规范病害
          <select
            disabled={disabled}
            value={defect.standard_defect_indicator_id ?? ""}
            onChange={(event) => {
              const selected = indicators.find((item) => item.id === event.target.value);
              if (selected) dispatch({
                type: "select_standard_defect_indicator",
                candidateId: defect.candidate_id,
                indicatorId: selected.id,
                indicatorName: selected.name,
              });
            }}
          >
            <option value="">请选择规范病害</option>
            {indicators.map((item) => <option key={item.id} value={item.id}>{item.name}</option>)}
          </select>
        </label>
        <label>位置<input disabled={disabled} value={defect.defect_location} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_location", value: event.target.value })} /></label>
        <label>
          标度
          <select
            disabled={disabled || !indicator}
            value={defect.defect_scale ?? ""}
            onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_scale", value: event.target.value === "" ? null : Number(event.target.value) })}
          >
            <option value="">请选择标度</option>
            {indicator?.allowed_scales.map((scale) => <option key={scale} value={scale}>{scale}</option>)}
          </select>
        </label>
        <label className="defect-detail-wide">病害描述<input disabled={disabled} value={defect.defect_description} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_description", value: event.target.value })} /></label>
      </div>
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
