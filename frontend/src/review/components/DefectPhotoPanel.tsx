import { useRef, useState, type Dispatch } from "react";

import { defectPhotoErrorMessage, deleteUploadedPhoto, uploadDefectPhoto } from "../../api/defectPhotoApi";
import { photoContentUrl } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData, DefectCandidate } from "../../contracts/annualInspection";
import type { DefectPhotoCard } from "../defectPhotoCards";
import type { ReviewDraftAction } from "../reviewDraft";

interface DefectPhotoPanelProps {
  draft: BridgeAnnualInspectionData;
  defect: DefectCandidate;
  /** 与复核模型算问题时用的是同一份卡片，界面和问题清单不会各说一套。 */
  cards: DefectPhotoCard[];
  importRecordId: string;
  baseUrl: string;
  initialPhotoCandidateId?: string | null;
  dispatch: Dispatch<ReviewDraftAction>;
  disabled?: boolean;
  /** 编辑锁令牌；没有令牌就上传不了，只能在导入内部改归属。 */
  editLockToken?: string | null;
  /** 与新增病害同一把闸：重开校对的仅警告范围不允许往导入里塞新东西。 */
  allowUpload?: boolean;
}

export function DefectPhotoPanel({
  draft,
  defect,
  cards,
  importRecordId,
  baseUrl,
  initialPhotoCandidateId,
  dispatch,
  disabled = false,
  editLockToken,
  allowUpload = false,
}: DefectPhotoPanelProps) {
  const [picking, setPicking] = useState(false);
  const [activeKey, setActiveKey] = useState<string | null>(initialPhotoCandidateId ?? null);
  const [caption, setCaption] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const fileRef = useRef<HTMLInputElement>(null);
  const active = cards.find((card) => card.key === activeKey)
    ?? cards.find((card) => card.kind === "photo")
    ?? null;
  const unassigned = draft.photos.filter((photo) => !photo.linked_defect_candidate_id);
  const canUpload = allowUpload && !disabled && Boolean(editLockToken);

  async function upload(file: File): Promise<void> {
    if (!editLockToken) return;
    setBusy(true);
    setError(null);
    try {
      const photo = await uploadDefectPhoto(baseUrl, importRecordId, editLockToken, {
        file,
        defectCandidateId: defect.candidate_id,
        caption: caption.trim(),
      });
      // 服务端已经把候选写进 parsed_result_json，本地草稿照抄同一份，两边不会分叉。
      dispatch({ type: "add_photo", photo });
      setCaption("");
      setPicking(false);
    } catch (uploadError) {
      setError(defectPhotoErrorMessage(uploadError));
    } finally {
      setBusy(false);
      if (fileRef.current) fileRef.current.value = "";
    }
  }

  async function removeCard(card: DefectPhotoCard): Promise<void> {
    const photo = card.photo;
    if (!photo) return;
    // Word 抽出的图退回未归属区，归档文件必须留痕；人工上传的可永久删除。
    // 两者共用一个按钮名，二次确认的措辞把区别说清楚。
    if (card.source === "manual") {
      if (!window.confirm("确定删除这张照片？将永久删除，无法恢复。")) return;
      if (!editLockToken) {
        setError("请先获取编辑权后再删除照片。");
        return;
      }
      setBusy(true);
      setError(null);
      try {
        await deleteUploadedPhoto(baseUrl, importRecordId, editLockToken, photo.candidate_id);
        dispatch({ type: "remove_photo", photoCandidateId: photo.candidate_id });
      } catch (deleteError) {
        setError(defectPhotoErrorMessage(deleteError));
      } finally {
        setBusy(false);
      }
      return;
    }
    if (!window.confirm("确定删除这张照片？它会退回未归属照片清单。")) return;
    dispatch({ type: "unlink_photo_from_defect", photoCandidateId: photo.candidate_id });
  }

  return (
    <section className="defect-photo-panel" aria-label="Word 引用的照片">
      <div className="defect-photo-panel-heading">
        <h4>Word 引用的照片</h4>
        <button
          type="button"
          disabled={disabled || busy || (unassigned.length === 0 && !canUpload)}
          title={unassigned.length === 0 && !canUpload ? "本次导入没有未归属的照片，也无法上传" : undefined}
          onClick={() => setPicking((open) => !open)}
        >
          添加照片
        </button>
      </div>

      {error ? <p className="form-error" role="alert">{error}</p> : null}

      {picking ? (
        <div className="defect-photo-picker">
          {unassigned.length > 0 ? (
            <div className="defect-photo-picker-existing" aria-label="未归属的照片">
              {unassigned.map((photo) => (
                <button
                  key={photo.candidate_id}
                  type="button"
                  disabled={disabled || busy}
                  onClick={() => {
                    dispatch({
                      type: "link_photo_to_defect",
                      photoCandidateId: photo.candidate_id,
                      defectCandidateId: defect.candidate_id,
                    });
                    setPicking(false);
                  }}
                >
                  <img loading="lazy" src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)} alt="" />
                  <span>{photo.photo_number}</span>
                </button>
              ))}
            </div>
          ) : (
            <p>本次导入没有未归属的照片。</p>
          )}

          {canUpload ? (
            <div className="defect-photo-upload">
              <label htmlFor={`photo-caption-${defect.candidate_id}`}>照片说明</label>
              <input
                id={`photo-caption-${defect.candidate_id}`}
                type="text"
                value={caption}
                disabled={busy}
                placeholder="将作为报告里的照片题注"
                onChange={(event) => setCaption(event.target.value)}
              />
              <input
                ref={fileRef}
                type="file"
                aria-label="从电脑上传照片"
                accept="image/jpeg,image/png,image/gif,image/bmp,image/webp,image/tiff"
                disabled={busy}
                onChange={(event) => {
                  const file = event.target.files?.[0];
                  if (file) void upload(file);
                }}
              />
              {busy ? <span>正在上传…</span> : null}
            </div>
          ) : null}
        </div>
      ) : null}

      {active?.photo ? (
        <div className="defect-photo-stage">
          <img
            src={photoContentUrl(baseUrl, importRecordId, active.photo.candidate_id)}
            alt={`照片 ${active.photo.photo_number}`}
          />
          {/* Word 图注是判断"这张图是不是这条病害"的第一手依据。 */}
          <p className="photo-relation-caption">
            {active.photo.extracted_file.original_caption ?? "无照片说明"}
          </p>
        </div>
      ) : null}

      {cards.length === 0 ? <p>这条病害还没有照片，Word 原文也没有照片编号。</p> : null}

      <div className="defect-photo-cards">
        {cards.map((card) => (
          <div
            key={card.key}
            className={`defect-photo-card ${card.kind} ${card.key === active?.key ? "active" : ""}`}
          >
            <button
              type="button"
              className="defect-photo-card-main"
              aria-label={`查看照片 ${card.photoNumber}`}
              onClick={() => setActiveKey(card.key)}
            >
              {card.photo ? (
                <img loading="lazy" src={photoContentUrl(baseUrl, importRecordId, card.photo.candidate_id)} alt="" />
              ) : (
                <span className="defect-photo-card-empty">无图</span>
              )}
              <strong>{card.photoNumber}</strong>
              {card.kind === "missing" ? (
                <small>{card.acknowledgedMissing ? "原报告缺图" : "待核对"}</small>
              ) : null}
            </button>

            <div className="defect-photo-card-actions">
              {card.kind === "missing" ? (
                <button
                  type="button"
                  disabled={disabled || busy}
                  onClick={() => dispatch({
                    type: "set_photo_reference_missing",
                    defectCandidateId: defect.candidate_id,
                    photoNumber: card.photoNumber,
                    missing: !card.acknowledgedMissing,
                  })}
                >
                  {card.acknowledgedMissing ? "撤销缺图" : "确认缺图"}
                </button>
              ) : (
                <button
                  type="button"
                  className="danger-text-button"
                  disabled={disabled || busy}
                  onClick={() => { void removeCard(card); }}
                >
                  删除照片
                </button>
              )}
            </div>
          </div>
        ))}
      </div>
    </section>
  );
}
