import { PlusOutlined } from "@ant-design/icons";
import {
  Alert,
  Button,
  Col,
  Empty,
  Flex,
  Image,
  Modal,
  Popconfirm,
  Row,
  Tag,
  Typography,
  Upload,
  type UploadProps,
} from "antd";
import { useState } from "react";

import {
  BRIDGE_MEDIA_SLOTS,
  bridgeMediaContentUrl,
  bridgeMediaError,
  deleteBridgeMedia,
  uploadBridgeMedia,
  type BridgeMedia,
  type BridgeMediaSlot,
} from "../api/bridgeMediaApi";
import { backendBaseUrl } from "../config";

const GROUPS: Array<{ group: "figure" | "photo"; title: string }> = [
  { group: "figure", title: "图" },
  { group: "photo", title: "照片" },
];

const ACCEPTED_TYPES = "image/png,image/jpeg,image/gif,image/bmp,image/webp,image/tiff";

/** 缩略图高度：弹窗 760 宽、一行三格时，大致是 16:10。 */
const THUMB_HEIGHT = 136;

/**
 * 桥梁图件。
 *
 * 两行三格，和报告里的两条编号对应：上一行是地理位置图和两张示意图，共用图号；下一行是
 * 三张照片，另起编号。一个格子一张图。
 *
 * 界面上只留图和图名，不写说明文字——这是用户定下的。地理位置图不必传，生成报告时按档案
 * 坐标自动取；管理员自己传的那张优先。
 *
 * 放在弹窗里而不是总览页上：总览页刚压到一屏放下四张卡片，六张缩略图摆出来就又出滚动条了。
 */
export function BridgeMediaDialog({
  bridgeId,
  bridgeName,
  media,
  canEdit,
  onReplaced,
  onRemoved,
  onClose,
}: {
  bridgeId: string;
  bridgeName: string;
  media: BridgeMedia[];
  canEdit: boolean;
  onReplaced: (item: BridgeMedia) => void;
  onRemoved: (slot: BridgeMediaSlot) => void;
  onClose: () => void;
}) {
  const [busySlot, setBusySlot] = useState<BridgeMediaSlot | null>(null);
  const [error, setError] = useState<string | null>(null);

  async function upload(slot: BridgeMediaSlot, file: File) {
    setBusySlot(slot);
    setError(null);
    try {
      onReplaced(await uploadBridgeMedia(backendBaseUrl, bridgeId, slot, file));
    } catch (caught) {
      setError(bridgeMediaError(caught));
    } finally {
      setBusySlot(null);
    }
  }

  async function remove(slot: BridgeMediaSlot) {
    setBusySlot(slot);
    setError(null);
    try {
      await deleteBridgeMedia(backendBaseUrl, bridgeId, slot);
      onRemoved(slot);
    } catch (caught) {
      setError(bridgeMediaError(caught));
    } finally {
      setBusySlot(null);
    }
  }

  // 选中或拖进来的文件直接传到这一格，不进 antd 自己的文件列表。
  function slotUpload(slot: BridgeMediaSlot, busy: boolean): UploadProps {
    return {
      accept: ACCEPTED_TYPES,
      showUploadList: false,
      disabled: busy,
      beforeUpload: (file) => {
        void upload(slot, file);
        return Upload.LIST_IGNORE;
      },
    };
  }

  return (
    <Modal
      open
      centered
      width={760}
      title={
        <Flex align="center" gap={8}>
          图件 · {bridgeName}
          <Tag color="blue" variant="filled">
            {media.length} / {BRIDGE_MEDIA_SLOTS.length}
          </Tag>
        </Flex>
      }
      footer={<Button onClick={onClose}>关闭</Button>}
      onCancel={onClose}
    >
      <Flex vertical gap={16}>
        {GROUPS.map(({ group, title }) => (
          <section key={group} aria-label={title}>
            <Typography.Title level={5}>{title}</Typography.Title>
            <Row gutter={[14, 14]} role="list">
              {BRIDGE_MEDIA_SLOTS.filter((entry) => entry.group === group).map(({ slot, label, name }) => {
                const item = media.find((entry) => entry.slot === slot);
                const busy = busySlot === slot;
                const picture = item ? (
                  <Image
                    src={bridgeMediaContentUrl(backendBaseUrl, item)}
                    alt={label}
                    width="100%"
                    height={THUMB_HEIGHT}
                    styles={{ image: { objectFit: "cover" } }}
                  />
                ) : null;

                let frame;
                if (item && canEdit) {
                  // 已有图的格子也能直接拖一张新图进来替换；单击仍是看大图。
                  frame = (
                    <Upload
                      {...slotUpload(slot, busy)}
                      openFileDialogOnClick={false}
                      styles={{ trigger: { display: "block" } }}
                    >
                      {picture}
                    </Upload>
                  );
                } else if (item) {
                  frame = picture;
                } else if (canEdit) {
                  frame = (
                    <Upload.Dragger {...slotUpload(slot, busy)} height={THUMB_HEIGHT} aria-label={`上传${label}`}>
                      <PlusOutlined aria-label={`添加${label}`} />
                    </Upload.Dragger>
                  );
                } else {
                  frame = (
                    <Flex align="center" justify="center" style={{ height: THUMB_HEIGHT }}>
                      <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={false} />
                    </Flex>
                  );
                }

                return (
                  <Col key={slot} span={8} role="listitem" aria-label={name} aria-busy={busy || undefined}>
                    <Flex vertical gap={6}>
                      {frame}
                      <Flex align="center" justify="space-between" gap={6}>
                        <Typography.Text ellipsis>{name}</Typography.Text>
                        {canEdit && item ? (
                          <Flex gap={4}>
                            <Upload {...slotUpload(slot, busy)} aria-label={`替换${label}`}>
                              <Button size="small" disabled={busy}>替换</Button>
                            </Upload>
                            <Popconfirm
                              title={`删除「${label}」？`}
                              okText="删除"
                              cancelText="取消"
                              okButtonProps={{ danger: true }}
                              onConfirm={() => remove(slot)}
                            >
                              <Button size="small" disabled={busy}>删除</Button>
                            </Popconfirm>
                          </Flex>
                        ) : null}
                      </Flex>
                    </Flex>
                  </Col>
                );
              })}
            </Row>
          </section>
        ))}

        {error ? <Alert type="error" showIcon title={error} /> : null}
      </Flex>
    </Modal>
  );
}
