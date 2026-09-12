import { useState, type Dispatch } from "react";
import { Button, Empty, Input, Modal, Upload } from "antd";
import { InboxOutlined } from "@ant-design/icons";

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

const PICKER_DIALOG_WIDTH = 560;
const PICKER_DIALOG_BODY_MAX_HEIGHT = "56vh";
const PHOTO_ACCEPT = "image/jpeg,image/png,image/gif,image/bmp,image/webp,image/tiff";

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
  /* 选中的文件先暂存，由弹窗底部的「上传」提交。原来是选完文件立刻上传，于是
     题注必须抢在选文件之前填——顺手先选图的人会把说明整个丢掉。 */
  const [pendingFile, setPendingFile] = useState<File | null>(null);

  const closePicker = () => {
    setPicking(false);
    setCaption("");
    setPendingFile(null);
  };
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
      setPendingFile(null);
      setPicking(false);
    } catch (uploadError) {
      setError(defectPhotoErrorMessage(uploadError));
    } finally {
      setBusy(false);
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

  function detachReference(card: DefectPhotoCard): void {
    // 引用条目删掉就没有回头路——"撤销缺图"只能把 resolution 翻回 pending，翻不回
    // 一条已经不在清单里的条目。所以这里要二次确认，措辞说清是"引用"不是"照片"。
    const subject = card.photoNumber ? `照片编号 ${card.photoNumber} 的` : "这张照片的";
    if (!window.confirm(
      `确定移除${subject}引用？本条病害将不再声明这张照片，且无法撤销。`,
    )) {
      return;
    }
    dispatch({
      type: "remove_photo_reference",
      defectCandidateId: defect.candidate_id,
      photoNumber: card.photoNumber,
      photoCandidateId: card.photo?.candidate_id ?? null,
    });
  }

  return (
    <section className="defect-photo-panel" aria-label="病害照片">
      <div className="defect-photo-panel-heading">
        <h4>照片与证据</h4>
      </div>

      {error ? <p className="form-error" role="alert">{error}</p> : null}

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
              aria-label={card.photoNumber ? `查看照片 ${card.photoNumber}` : "查看照片"}
              onClick={() => setActiveKey(card.key)}
            >
              {card.photo ? (
                <img loading="lazy" src={photoContentUrl(baseUrl, importRecordId, card.photo.candidate_id)} alt="" />
              ) : (
                <span className="defect-photo-card-empty">无图</span>
              )}
              {/* 来源软件导入没有照片编号（靠外键绑定），这里不显示占位文字。 */}
              {card.photoNumber ? <strong>{card.photoNumber}</strong> : null}
              {card.kind === "missing" ? (
                <small>{card.acknowledgedMissing ? "原报告缺图" : "待核对"}</small>
              ) : null}
            </button>

            <div className="defect-photo-card-actions">
              {card.kind === "missing" ? (
                <>
                  <button
                    type="button"
                    disabled={disabled || busy}
                    onClick={() => dispatch({
                      type: "set_photo_reference_missing",
                      defectCandidateId: defect.candidate_id,
                      photoNumber: card.photoNumber,
                      photoCandidateId: card.photo?.candidate_id ?? null,
                      missing: !card.acknowledgedMissing,
                    })}
                  >
                    {card.acknowledgedMissing ? "撤销缺图" : "确认缺图"}
                  </button>
                  {/* 拆分复制来的引用在这儿了结：图是真的，只是它属于另一条病害。
                      与"确认缺图"分成两个按钮，是因为两者对报告的结论完全相反。 */}
                  <button
                    type="button"
                    className="danger-text-button"
                    disabled={disabled || busy}
                    title="该引用不属于本病害：把这个照片编号从本条病害的 Word 引用里移除"
                    onClick={() => { detachReference(card); }}
                  >
                    不属于本病害
                  </button>
                </>
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

      <div className="defect-photo-panel-actions">
        <button
          type="button"
          aria-label="添加照片"
          disabled={disabled || busy || (unassigned.length === 0 && !canUpload)}
          title={unassigned.length === 0 && !canUpload ? "本次导入没有未归属的照片，也无法上传" : undefined}
          onClick={() => (picking ? closePicker() : setPicking(true))}
        >
          ＋ 添加照片
        </button>
        {active?.photo ? (
          <a
            href={photoContentUrl(baseUrl, importRecordId, active.photo.candidate_id)}
            target="_blank"
            rel="noreferrer"
          >查看原图</a>
        ) : <span className="defect-photo-original-disabled">查看原图</span>}
      </div>

      {/* 添加照片是操作类浮层：body 是唯一滚动区，底部主操作恒可见。 */}
      <Modal
        open={picking}
        title="添加照片"
        centered
        width={PICKER_DIALOG_WIDTH}
        maskClosable={!busy}
        onCancel={closePicker}
        footer={canUpload ? [
          <Button key="cancel" disabled={busy} onClick={closePicker}>取消</Button>,
          <Button
            key="upload"
            type="primary"
            loading={busy}
            disabled={!pendingFile}
            onClick={() => { if (pendingFile) void upload(pendingFile); }}
          >上传</Button>,
        ] : [
          <Button key="close" onClick={closePicker}>关闭</Button>,
        ]}
        styles={{ body: { maxHeight: PICKER_DIALOG_BODY_MAX_HEIGHT, overflowY: "auto" } }}
      >
        {unassigned.length > 0 ? (
          <>
            <p className="defect-photo-dialog-hint">从本次导入的未归属照片里选一张</p>
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
                    closePicker();
                  }}
                >
                  <img loading="lazy" src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)} alt="" />
                  <span>{photo.photo_number}</span>
                </button>
              ))}
            </div>
          </>
        ) : (
          <Empty
            image={Empty.PRESENTED_IMAGE_SIMPLE}
            description={canUpload
              ? "本次导入没有未归属的照片，可从电脑上传一张。"
              : "本次导入没有未归属的照片。"}
          />
        )}

        {canUpload ? (
          <div className={unassigned.length > 0 ? "defect-photo-upload defect-photo-upload-divided" : "defect-photo-upload"}>
            <Upload.Dragger
              accept={PHOTO_ACCEPT}
              maxCount={1}
              disabled={busy}
              beforeUpload={(file) => { setPendingFile(file); return false; }}
              onRemove={() => { setPendingFile(null); return true; }}
              fileList={pendingFile ? [{ uid: "pending", name: pendingFile.name, status: "done" as const }] : []}
            >
              <p className="ant-upload-drag-icon"><InboxOutlined /></p>
              <p className="ant-upload-text">点击或把照片拖到这里</p>
              <p className="ant-upload-hint">一次一张，支持 JPG / PNG / GIF / BMP / WebP / TIFF</p>
            </Upload.Dragger>
            <label className="defect-photo-caption-field">
              <span>照片说明</span>
              <Input
                value={caption}
                disabled={busy}
                placeholder="将作为报告里的照片题注"
                onChange={(event) => setCaption(event.target.value)}
              />
            </label>
          </div>
        ) : null}
      </Modal>
    </section>
  );
}
