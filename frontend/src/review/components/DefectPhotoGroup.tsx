import { useEffect, useState, type CSSProperties, type Dispatch, type MouseEvent } from "react";

import { photoContentUrl } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData, DefectCandidate, ReviewStatus, StructurePart } from "../../contracts/annualInspection";
import { categoryColor } from "../categoryColor";
import { buildDefectPhotoGroup, canConfirmDefectPhotoGroup } from "../defectPhotoGroups";
import type { ReviewDraftAction } from "../reviewDraft";

const STRUCTURE_PARTS: StructurePart[] = ["全桥", "上部结构", "下部结构", "桥面系", "其他"];
const REVIEW_STATUSES: ReviewStatus[] = ["待确认", "已确认", "已修改", "已忽略"];

interface DefectPhotoGroupProps {
  draft: BridgeAnnualInspectionData;
  defect: DefectCandidate;
  importRecordId: string;
  baseUrl: string;
  expanded: boolean;
  onToggle: () => void;
  dispatch: Dispatch<ReviewDraftAction>;
  initialPhotoCandidateId?: string | null;
  disabled?: boolean;
}

function parsePhotoNumbers(text: string): string[] {
  return text.split(/[,，\s]+/).map((value) => value.trim()).filter(Boolean);
}

function keepRowOpen(event: MouseEvent<HTMLElement>): void {
  event.stopPropagation();
}

// 照片校对状态徽章：沿用页眉的 review-status-badge 配色（待确认=黄、已确认=绿、其余=灰）。
function reviewStatusBadgeClass(status: ReviewStatus): string {
  if (status === "已确认") return "review-status-badge review-status-confirmed";
  if (status === "待确认") return "review-status-badge review-status-pending";
  return "review-status-badge review-status-neutral";
}

// 禁用策略（逐控件而非外层 fieldset 一揽子禁用）：编辑类控件跟随 disabled；
// 查看类控件（"查看照片"toggle、缩略图切换、大图展示）永不禁用——
// 查看是只读动作，已确认/重开范围外的病害也必须能看照片。
export function DefectPhotoGroup({ draft, defect, importRecordId, baseUrl, expanded, onToggle, dispatch, initialPhotoCandidateId, disabled = false }: DefectPhotoGroupProps) {
  const group = buildDefectPhotoGroup(draft, defect.candidate_id);
  const photos = group?.photos ?? [];
  const [activePhotoId, setActivePhotoId] = useState(initialPhotoCandidateId ?? photos[0]?.candidate_id ?? null);
  const activePhoto = photos.find((photo) => photo.candidate_id === activePhotoId) ?? photos[0] ?? null;
  const confirmation = canConfirmDefectPhotoGroup(draft, defect.candidate_id);

  useEffect(() => {
    if (!photos.some((photo) => photo.candidate_id === activePhotoId)) {
      setActivePhotoId(photos[0]?.candidate_id ?? null);
    }
  }, [activePhotoId, photos]);

  useEffect(() => {
    if (initialPhotoCandidateId && photos.some((photo) => photo.candidate_id === initialPhotoCandidateId)) setActivePhotoId(initialPhotoCandidateId);
  }, [initialPhotoCandidateId, photos]);

  const groupClassName = [
    "defect-photo-group",
    expanded ? "expanded" : "",
    defect.group_review_status === "已确认" ? "confirmed" : "pending",
    disabled ? "controls-disabled" : "",
  ].filter(Boolean).join(" ");

  return (
    <tbody className={groupClassName} aria-disabled={disabled} style={{ "--defect-category-color": categoryColor(defect.component_name) } as CSSProperties}>
      <tr className="defect-card-row">
        <td>
          {/* 整卡统一表单网格：六个汇总字段与数量/尺寸原文/照片编号用同一套"标签 + 输入框"语言，
              标签右对齐同一列、输入框共享左边线，消除汇总行与明细行之间的割裂感。 */}
          <div className="defect-fact-grid">
            <span className="defect-fact-label defect-fact-label-row-start">结构部位</span>
            <select aria-label="结构部位" disabled={disabled} value={defect.structure_part} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "structure_part", value: event.target.value as StructurePart })}>{STRUCTURE_PARTS.map((part) => <option key={part}>{part}</option>)}</select>
            <span className="defect-fact-label">构件类别</span>
            <input aria-label="构件类别" disabled={disabled} value={defect.component_name} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "component_name", value: event.target.value })} />
            <span className="defect-fact-label">构件编号</span>
            <input aria-label="构件编号" disabled={disabled} value={defect.component_alias ?? ""} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "component_alias", value: event.target.value || null })} />
            <span className="defect-fact-label defect-fact-label-row-start">位置</span>
            <input aria-label="位置" disabled={disabled} value={defect.defect_location} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_location", value: event.target.value })} />
            <span className="defect-fact-label">病害类型</span>
            <input aria-label="病害类型" disabled={disabled} value={defect.defect_type} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_type", value: event.target.value })} />
            <span className="defect-fact-label">校对状态</span>
            <select aria-label="校对状态" disabled={disabled} value={defect.review_status} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "review_status", value: event.target.value as ReviewStatus })}>{REVIEW_STATUSES.map((status) => <option key={status}>{status}</option>)}</select>
            {/* 合同 1.2：规范标度（正整数）与病害扣分（0-100）。空输入回落为 null；
                扣分编辑会触发所属构件评分候选的复算校验联动。 */}
            <span className="defect-fact-label defect-fact-label-row-start">标度</span>
            <input aria-label="标度" disabled={disabled} type="number" min={1} step={1} value={defect.defect_scale ?? ""} onClick={keepRowOpen} onChange={(event) => { const raw = event.target.value; const parsed = raw === "" ? null : Number.parseInt(raw, 10); if (parsed !== null && (!Number.isInteger(parsed) || parsed <= 0)) return; dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_scale", value: parsed }); }} />
            <span className="defect-fact-label">病害扣分</span>
            <input aria-label="病害扣分" disabled={disabled} type="number" min={0} max={100} step={0.1} value={defect.defect_deduction ?? ""} onClick={keepRowOpen} onChange={(event) => { const raw = event.target.value; const parsed = raw === "" ? null : Number(raw); if (parsed !== null && (!Number.isFinite(parsed) || parsed < 0 || parsed > 100)) return; dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "defect_deduction", value: parsed }); }} />
            <span className="defect-fact-label defect-fact-label-row-start">数量</span>
            <input aria-label="数量" disabled={disabled} value={defect.quantity_text ?? ""} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "quantity_text", value: event.target.value })} />
            <span className="defect-fact-label">照片编号</span>
            <input aria-label="照片编号" disabled={disabled} value={defect.photo_numbers.join(", ")} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "photo_numbers", value: parsePhotoNumbers(event.target.value) })} />
            <button type="button" className="defect-photo-toggle" aria-expanded={expanded} onClick={onToggle}>{expanded ? "收起照片" : `查看照片（${photos.length}）`}</button>
            <span className="defect-fact-label defect-fact-label-row-start">尺寸原文</span>
            <input className="defect-fact-span" aria-label="尺寸原文" disabled={disabled} title={defect.measurement_text ?? ""} value={defect.measurement_text ?? ""} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_measurement_text", candidateId: defect.candidate_id, text: event.target.value })} />
            {expanded ? (
              <>
                <span className="defect-fact-label defect-fact-label-row-start">校对备注</span>
                <input className="defect-fact-span" aria-label="备注" disabled={disabled} placeholder="填写校对说明…" value={defect.review_note ?? ""} onClick={keepRowOpen} onChange={(event) => dispatch({ type: "edit_defect_field", candidateId: defect.candidate_id, field: "review_note", value: event.target.value })} />
              </>
            ) : null}
          </div>
        </td>
      </tr>
      {expanded ? (
        <tr className="defect-photo-detail-row">
          <td>
            <div className="defect-photo-review">
              {/* 照片校对区：浅底色圆角块把"正在校对哪张照片"框出来，与上方字段区分区。 */}
              <div className="defect-photo-zone">
                <div className="defect-photo-stage">
                  {activePhoto ? <img className="defect-photo-stage-image active" src={photoContentUrl(baseUrl, importRecordId, activePhoto.candidate_id)} alt={`照片 ${activePhoto.photo_number}`} /> : <p>暂无已关联照片。</p>}
                </div>
                <div className="defect-photo-meta">
                  {activePhoto ? (
                    <>
                      <div className="defect-photo-meta-head">
                        <strong>照片 {activePhoto.photo_number}</strong>
                        <span className="severity-badge severity-info">{activePhoto.match_status}</span>
                        <span className={reviewStatusBadgeClass(activePhoto.review_status)}>{activePhoto.review_status}</span>
                      </div>
                      <span className="defect-photo-caption">{activePhoto.extracted_file.original_caption ?? "无照片说明"}</span>
                      <div className="review-photo-actions">
                        <button type="button" className="review-action-primary" disabled={disabled} onClick={() => dispatch({ type: "photo_confirm_match", candidateId: activePhoto.candidate_id })}>照片正确</button>
                        <button type="button" disabled={disabled} onClick={() => dispatch({ type: "photo_reset", candidateId: activePhoto.candidate_id })}>重置</button>
                        <button type="button" disabled={disabled} onClick={() => dispatch({ type: "photo_mark_unrelated", candidateId: activePhoto.candidate_id, note: "人工确认与病害无关" })}>确认无关</button>
                        <button type="button" disabled={disabled} onClick={() => dispatch({ type: "photo_ignore", candidateId: activePhoto.candidate_id })}>忽略</button>
                      </div>
                      {/* 选项用"构件编号 / 位置 / 病害类型"定位病害（编号缺失时回退到构件类别）。 */}
                      <label className="defect-photo-relink">重新关联<select disabled={disabled} value={activePhoto.linked_defect_candidate_id ?? ""} onChange={(event) => event.target.value && dispatch({ type: "photo_relink", candidateId: activePhoto.candidate_id, defectCandidateId: event.target.value })}>{draft.defects.map((item) => <option key={item.candidate_id} value={item.candidate_id}>{item.component_alias ?? item.component_name} / {item.defect_location} / {item.defect_type}</option>)}</select></label>
                    </>
                  ) : null}
                  {photos.length > 0 ? <div className="defect-photo-thumbnails">{photos.map((photo) => <button key={photo.candidate_id} type="button" className={photo.candidate_id === activePhoto?.candidate_id ? "active" : ""} aria-label={`查看照片 ${photo.photo_number}`} onClick={() => setActivePhotoId(photo.candidate_id)}><img src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)} alt="" /><span>{photo.photo_number}</span></button>)}</div> : null}
                </div>
              </div>
              {(group?.missingPhotoNumbers.length ?? 0) > 0 ? <div className="missing-photo-list"><strong>Word 中引用但未找到的照片</strong>{group?.missingPhotoNumbers.map((number) => { const confirmed = defect.confirmed_missing_photo_numbers.includes(number); return <div key={number}><span>照片 {number}</span><button type="button" disabled={disabled} onClick={() => dispatch({ type: confirmed ? "unconfirm_missing_photo" : "confirm_missing_photo", defectCandidateId: defect.candidate_id, photoNumber: number })}>{confirmed ? "撤销缺图确认" : "人工确认缺图"}</button></div>; })}</div> : null}
              <div className="defect-group-confirm"><span>{confirmation.ok ? "病害与照片均已具备确认条件。" : `尚不能确认：${confirmation.reasons.join("、")}`}</span><button type="button" disabled={disabled || !confirmation.ok} onClick={() => dispatch({ type: "confirm_defect_group", defectCandidateId: defect.candidate_id })}>确认本组</button></div>
            </div>
          </td>
        </tr>
      ) : null}
      <tr className="defect-group-spacer" aria-hidden="true"><td /></tr>
    </tbody>
  );
}
