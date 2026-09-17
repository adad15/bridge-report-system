import { Button, Card, Flex, Image, Typography, theme } from "antd";
import { useEffect, useState } from "react";

import { photoContentUrl } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import { reviewTargetId } from "../reviewNavigation";

interface UnlinkedPhotosPanelProps {
  draft: BridgeAnnualInspectionData;
  importRecordId: string;
  baseUrl: string;
  selectedPhotoCandidateId?: string | null;
}

/**
 * 只读清单：告诉用户还有多少张图没有着落，可预览，但不带归属操作。
 * 把图挂到病害上只有一个入口——病害卡片里的「添加照片」，不再有第二套说法。
 */
export function UnlinkedPhotosPanel({
  draft,
  importRecordId,
  baseUrl,
  selectedPhotoCandidateId,
}: UnlinkedPhotosPanelProps) {
  const { token } = theme.useToken();
  const photos = draft.photos.filter((photo) => !photo.linked_defect_candidate_id);
  const [activeId, setActiveId] = useState(selectedPhotoCandidateId ?? photos[0]?.candidate_id ?? null);
  const active = photos.find((photo) => photo.candidate_id === activeId) ?? photos[0] ?? null;

  useEffect(() => {
    if (selectedPhotoCandidateId && photos.some((photo) => photo.candidate_id === selectedPhotoCandidateId)) {
      setActiveId(selectedPhotoCandidateId);
    }
  }, [photos, selectedPhotoCandidateId]);

  if (photos.length === 0) return null;
  return (
    <Card
      size="small"
      id={reviewTargetId("unlinked-photos", "panel")}
      tabIndex={-1}
      title={<Typography.Title level={5} style={{ margin: 0 }}>{`未归属的照片（${photos.length} 张）`}</Typography.Title>}
    >
      <Flex vertical gap={10}>
        <Typography.Text type="secondary">
          这些照片还没有挂到任何病害上。要归属其中一张，请在对应病害里点「添加照片」。
        </Typography.Text>

        {active ? (
          <Flex vertical gap={6}>
            <Image
              src={photoContentUrl(baseUrl, importRecordId, active.candidate_id)}
              alt={`照片 ${active.photo_number}`}
              style={{ maxHeight: 280, objectFit: "contain" }}
            />
            <Flex align="baseline" gap={10} wrap>
              <Typography.Text strong>照片 {active.photo_number}</Typography.Text>
              <Typography.Text type="secondary">
                {active.extracted_file.original_caption ?? "无照片说明"}
              </Typography.Text>
            </Flex>
          </Flex>
        ) : null}

        <Flex gap={8} wrap>
          {photos.map((photo) => (
            <Button
              key={photo.candidate_id}
              id={reviewTargetId("unlinked-photo", photo.candidate_id)}
              type="text"
              aria-label={`查看未归属照片 ${photo.photo_number}`}
              aria-current={photo.candidate_id === active?.candidate_id ? "true" : undefined}
              style={{
                height: "auto",
                padding: 4,
                background: photo.candidate_id === active?.candidate_id ? token.colorPrimaryBg : undefined,
              }}
              onClick={() => setActiveId(photo.candidate_id)}
            >
              <Flex vertical align="center" gap={2}>
                <img
                  loading="lazy"
                  src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)}
                  alt=""
                  style={{ width: 76, height: 56, objectFit: "cover", borderRadius: token.borderRadius }}
                />
                <Typography.Text type="secondary">{photo.photo_number}</Typography.Text>
              </Flex>
            </Button>
          ))}
        </Flex>
      </Flex>
    </Card>
  );
}
