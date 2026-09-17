import { useState, type Dispatch } from "react";
import { Alert, Button, Empty, Flex, Form, Image, Input, Modal, Tag, Tooltip, Typography, Upload, theme } from "antd";
import { ExpandOutlined, InboxOutlined, PlusOutlined } from "@ant-design/icons";

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
  const { token } = theme.useToken();

  const closePicker = () => {
    setPicking(false);
    setCaption("");
    setPendingFile(null);
  };
  // 默认优先看有图的那张；全是缺图引用时也要选中一张，否则"确认缺图"这类操作没有落点。
  const active = cards.find((card) => card.key === activeKey)
    ?? cards.find((card) => card.kind === "photo")
    ?? cards[0]
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

  const activeIndex = active ? cards.findIndex((card) => card.key === active.key) : -1;
  const canAdd = !(unassigned.length === 0 && !canUpload);
  const thumbSize = { width: 84, height: 60 };

  return (
    <Flex vertical gap={10} role="group" aria-label="病害照片">
      <Flex align="center" justify="space-between" gap={8}>
        <Typography.Text strong>
          照片与证据 <Typography.Text type="secondary">{cards.length}</Typography.Text>
        </Typography.Text>
        {active?.photo ? (
          <Tooltip title="在新窗口查看原图">
            <Button
              type="text"
              size="small"
              aria-label="查看原图"
              icon={<ExpandOutlined />}
              href={photoContentUrl(baseUrl, importRecordId, active.photo.candidate_id)}
              target="_blank"
              rel="noreferrer"
            />
          </Tooltip>
        ) : null}
      </Flex>

      {error ? <Alert type="error" showIcon role="alert" title={error} /> : null}

      {cards.length === 0 ? (
        <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="这条病害还没有照片，Word 原文也没有照片编号。" />
      ) : null}

      {active ? (
        <Flex vertical gap={6}>
          <div style={{ position: "relative", borderRadius: token.borderRadiusLG, overflow: "hidden", background: token.colorFillTertiary }}>
            {active.photo ? (
              <Image
                src={photoContentUrl(baseUrl, importRecordId, active.photo.candidate_id)}
                alt={`照片 ${active.photo.photo_number}`}
                width="100%"
                style={{ aspectRatio: "4 / 3", objectFit: "cover", display: "block" }}
              />
            ) : (
              <Flex vertical align="center" justify="center" gap={4} style={{ aspectRatio: "4 / 3" }}>
                <Typography.Text type="secondary">Word 原文引用了这张照片，但导入里没有找到</Typography.Text>
                <Typography.Text strong>{active.photoNumber ?? ""}</Typography.Text>
              </Flex>
            )}
            {active.photoNumber ? (
              <Tag
                variant="filled"
                style={{
                  position: "absolute",
                  insetInlineStart: 10,
                  insetBlockStart: 10,
                  margin: 0,
                  background: "rgba(23, 32, 51, 0.72)",
                  color: token.colorWhite,
                }}
              >
                {active.photoNumber} · {activeIndex + 1} / {cards.length}
              </Tag>
            ) : null}
          </div>
          {/* Word 图注是判断"这张图是不是这条病害"的第一手依据。 */}
          {active.photo ? (
            <Typography.Text type="secondary">
              {active.photo.extracted_file.original_caption ?? "无照片说明"}
            </Typography.Text>
          ) : null}

          {/* 写操作只对当前这一张：不再在每张缩略图下面各挂一个删除按钮。只读时整片不出现。 */}
          {disabled ? null : active.kind === "missing" ? (
            <Flex align="center" gap={8} wrap>
              <Typography.Text type="secondary">
                {active.acknowledgedMissing ? "已确认原报告缺图" : "待核对"}
              </Typography.Text>
              <Button
                size="small"
                disabled={busy}
                onClick={() => dispatch({
                  type: "set_photo_reference_missing",
                  defectCandidateId: defect.candidate_id,
                  photoNumber: active.photoNumber,
                  photoCandidateId: active.photo?.candidate_id ?? null,
                  missing: !active.acknowledgedMissing,
                })}
              >
                {active.acknowledgedMissing ? "撤销缺图" : "确认缺图"}
              </Button>
              {/* 拆分复制来的引用在这儿了结：图是真的，只是它属于另一条病害。
                  与"确认缺图"分成两个按钮，是因为两者对报告的结论完全相反。 */}
              <Button
                size="small"
                type="text"
                danger
                disabled={busy}
                title="该引用不属于本病害：把这个照片编号从本条病害的 Word 引用里移除"
                onClick={() => { detachReference(active); }}
              >
                不属于本病害
              </Button>
            </Flex>
          ) : (
            <Flex justify="end">
              <Button size="small" type="text" danger disabled={busy} onClick={() => { void removeCard(active); }}>
                删除照片
              </Button>
            </Flex>
          )}
        </Flex>
      ) : null}

      <Flex gap={8} wrap>
        {cards.map((card) => {
          const selected = card.key === active?.key;
          return (
            <Button
              key={card.key}
              type="text"
              aria-label={card.photoNumber ? `查看照片 ${card.photoNumber}` : "查看照片"}
              aria-current={selected ? "true" : undefined}
              onClick={() => setActiveKey(card.key)}
              style={{
                height: "auto",
                padding: 3,
                borderRadius: token.borderRadius,
                border: `2px solid ${selected ? token.colorPrimary : "transparent"}`,
                background: selected ? token.colorPrimaryBg : token.colorFillQuaternary,
              }}
            >
              <Flex vertical align="center" gap={2}>
                {card.photo ? (
                  <img
                    loading="lazy"
                    src={photoContentUrl(baseUrl, importRecordId, card.photo.candidate_id)}
                    alt=""
                    style={{ ...thumbSize, objectFit: "cover", borderRadius: token.borderRadiusSM, display: "block" }}
                  />
                ) : (
                  <Flex align="center" justify="center" style={{ ...thumbSize, borderRadius: token.borderRadiusSM, background: token.colorFillTertiary }}>
                    <Typography.Text type="secondary">{card.acknowledgedMissing ? "原报告缺图" : "无图"}</Typography.Text>
                  </Flex>
                )}
                {/* 来源软件导入没有照片编号（靠外键绑定），这里不显示占位文字。 */}
                {card.photoNumber ? <Typography.Text style={{ fontSize: token.fontSizeSM }}>{card.photoNumber}</Typography.Text> : null}
              </Flex>
            </Button>
          );
        })}
        {disabled ? null : (
          <Button
            type="dashed"
            aria-label="添加照片"
            disabled={busy || !canAdd}
            title={canAdd ? undefined : "本次导入没有未归属的照片，也无法上传"}
            onClick={() => (picking ? closePicker() : setPicking(true))}
            style={{ width: thumbSize.width + 10, height: thumbSize.height + 30 }}
          >
            <Flex vertical align="center" gap={2}>
              <PlusOutlined />
              <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>添加照片</Typography.Text>
            </Flex>
          </Button>
        )}
      </Flex>

      {/* 添加照片是操作类浮层：body 是唯一滚动区，底部主操作恒可见。 */}
      <Modal
        open={picking}
        title="添加照片"
        centered
        width={PICKER_DIALOG_WIDTH}
        mask={{ closable: !busy }}
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
            <Typography.Text type="secondary">从本次导入的未归属照片里选一张</Typography.Text>
            <Flex gap={10} wrap role="group" aria-label="未归属的照片" style={{ marginBlock: 10 }}>
              {unassigned.map((photo) => (
                <Button
                  key={photo.candidate_id}
                  type="text"
                  style={{ height: "auto", padding: 4 }}
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
                  <Flex vertical align="center" gap={4}>
                    <img
                      loading="lazy"
                      src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)}
                      alt=""
                      style={{ width: 92, height: 68, objectFit: "cover", borderRadius: token.borderRadius }}
                    />
                    <Typography.Text>{photo.photo_number}</Typography.Text>
                  </Flex>
                </Button>
              ))}
            </Flex>
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
          <Flex vertical gap={10} style={unassigned.length > 0 ? { borderTop: `1px solid ${token.colorSplit}`, paddingTop: 12 } : undefined}>
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
            <Form layout="vertical" style={{ marginBottom: 0 }}>
              <Form.Item label="照片说明" htmlFor="defect-photo-caption" style={{ marginBottom: 0 }}>
                <Input
                  id="defect-photo-caption"
                  value={caption}
                  disabled={busy}
                  placeholder="将作为报告里的照片题注"
                  onChange={(event) => setCaption(event.target.value)}
                />
              </Form.Item>
            </Form>
          </Flex>
        ) : null}
      </Modal>
    </Flex>
  );
}
