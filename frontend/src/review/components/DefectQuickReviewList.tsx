import { useEffect, useMemo, useRef, useState } from "react";
import { Button, Checkbox, Empty, Flex, Pagination, Table, Tag, Typography, theme, type TableColumnsType } from "antd";
import { PictureOutlined } from "@ant-design/icons";

import { photoContentUrl } from "../../api/reviewApi";
import type { DefectReviewRow } from "../defectPhotoReviewModel";
import { reviewTargetId } from "../reviewNavigation";
import { ratingTreeDisplayLabel as nodeLabel } from "../../rating-tree/ratingTreeLabels";
import { displayDefectLocation } from "./displayHelpers";

const DEFAULT_PAGE_SIZE = 20;

/// 已定评定树时给规范名（带条款号），未定时返回 null 由调用方兜底。
function ratingTreeDisplayLabel(node: DefectReviewRow["ratingTreeNode"]): string | null {
  return node ? nodeLabel(node) : null;
}

/// 报告原文；与规范名一致时返回空串，免得同一行写两遍。
function sourceWording(row: DefectReviewRow): string {
  const raw = row.defect.defect_type?.trim() ?? "";
  if (!raw) return "";
  return row.ratingTreeNode && row.ratingTreeNode.display_name === raw ? "" : raw;
}

/** 状态色：已确认绿、待处理琥珀、已忽略灰，与工具栏的筛选口径一致。 */
function statusTagColor(row: DefectReviewRow): string {
  if (row.defect.group_review_status === "已确认") return "success";
  if (row.status === "ignored") return "default";
  if (row.status === "batchable") return "processing";
  return "warning";
}

function statusText(row: DefectReviewRow): string {
  if (row.defect.group_review_status === "已确认") return "已确认";
  if (row.status === "ignored") return "已忽略";
  return row.matchLabel;
}

interface DefectQuickReviewListProps {
  rows: DefectReviewRow[];
  importRecordId: string;
  baseUrl: string;
  selectedCandidateIds: Set<string>;
  activeCandidateId: string | null;
  onToggleSelection: (candidateId: string) => void;
  onOpen: (candidateId: string, photoCandidateId?: string) => void;
  onPageChange?: () => void;
  compact?: boolean;
  /** 只读时不给勾选框：勾了也没有能执行的批量动作。 */
  selectable?: boolean;
}

export function DefectQuickReviewList({
  rows,
  importRecordId,
  baseUrl,
  selectedCandidateIds,
  activeCandidateId,
  onToggleSelection,
  onOpen,
  onPageChange,
  compact = false,
  selectable = true,
}: DefectQuickReviewListProps) {
  const { token } = theme.useToken();
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(DEFAULT_PAGE_SIZE);
  const pageCount = Math.max(1, Math.ceil(rows.length / pageSize));
  const currentPage = Math.min(page, pageCount);
  const compactItemsRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!activeCandidateId) return;
    const index = rows.findIndex((row) => row.candidateId === activeCandidateId);
    if (index >= 0) setPage(Math.floor(index / pageSize) + 1);
  }, [activeCandidateId, pageSize, rows]);

  useEffect(() => {
    setPage((current) => Math.min(current, pageCount));
  }, [pageCount]);

  const pageRows = useMemo(
    () => rows.slice((currentPage - 1) * pageSize, currentPage * pageSize),
    [currentPage, pageSize, rows],
  );

  // 紧凑列表在自己的框里滚。点"上一条/下一条"换了当前项，要把它滚进框里，
  // 只动这个框的 scrollTop：scrollIntoView 会连外层整页一起滚。
  useEffect(() => {
    const container = compactItemsRef.current;
    if (!compact || !container || !activeCandidateId) return;
    const item = Array.from(container.children).find(
      (child): child is HTMLElement => child instanceof HTMLElement && child.dataset.candidateId === activeCandidateId,
    );
    if (!item) return;
    const bounds = container.getBoundingClientRect();
    const itemBounds = item.getBoundingClientRect();
    if (itemBounds.top < bounds.top) container.scrollTop -= bounds.top - itemBounds.top;
    else if (itemBounds.bottom > bounds.bottom) container.scrollTop += itemBounds.bottom - bounds.bottom;
  }, [activeCandidateId, compact, currentPage]);

  const selectCell = (row: DefectReviewRow) => (
    <Checkbox
      aria-label={`选择病害 ${row.defect.component_number ?? row.candidateId}`}
      title={row.batchEligible ? "加入批量确认" : "此病害需人工处理"}
      disabled={!row.batchEligible}
      checked={selectedCandidateIds.has(row.candidateId)}
      onChange={() => onToggleSelection(row.candidateId)}
    />
  );

  const componentCell = (row: DefectReviewRow) => (
    <Button
      type="link"
      style={{ padding: 0, height: "auto", textAlign: "left", whiteSpace: "normal" }}
      onClick={() => onOpen(row.candidateId)}
    >
      <Flex vertical>
        <Typography.Text strong>{row.defect.component_number ?? row.defect.component_name}</Typography.Text>
        {/* 位置多为 "/"（Word 里的"无"），空着就不占这一行。 */}
        {displayDefectLocation(row.defect.defect_location) ? (
          <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
            {displayDefectLocation(row.defect.defect_location)}
          </Typography.Text>
        ) : null}
      </Flex>
    </Button>
  );

  const photosCell = (row: DefectReviewRow) => (
    <Flex align="center" gap={4} wrap>
      {row.photos.slice(0, 3).map((photo) => (
        <Button
          key={photo.candidate_id}
          type="text"
          style={{ height: "auto", padding: 2 }}
          aria-label={`打开照片 ${photo.photo_number}`}
          onClick={() => onOpen(row.candidateId, photo.candidate_id)}
        >
          <img
            loading="lazy"
            src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)}
            alt=""
            style={{ width: 38, height: 28, objectFit: "cover", borderRadius: token.borderRadiusSM }}
          />
        </Button>
      ))}
      {row.photos.length === 0 ? (
        <Typography.Text type="secondary" aria-hidden="true"><PictureOutlined /></Typography.Text>
      ) : null}
      <Typography.Text type="secondary">{row.photos.length} 张</Typography.Text>
    </Flex>
  );

  const columns: TableColumnsType<DefectReviewRow> = [
    ...(selectable
      ? [{ title: "", key: "select", width: 46, align: "center" as const, render: (_value: unknown, row: DefectReviewRow) => selectCell(row) }]
      : []),
    { title: "构件与位置", key: "component", width: 190, render: (_value, row) => componentCell(row) },
    {
      // 规范名与报告原文并列。校对这件事本身就是拿原文核对系统的判断，只给一个都不够：
      // 只给原文看不出定成了哪种规范病害，只给规范名就没法对着纸质报告逐行核。
      title: "病害类型",
      key: "defect",
      render: (_value, row) => (
        <Flex vertical>
          <Typography.Text strong>{ratingTreeDisplayLabel(row.ratingTreeNode) ?? "未确定规范病害"}</Typography.Text>
          {sourceWording(row) ? (
            <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
              原文：{sourceWording(row)}
            </Typography.Text>
          ) : null}
          {row.problems.slice(0, 2).map((problem) => (
            <Typography.Text key={problem.code} type="warning" style={{ fontSize: token.fontSizeSM }}>
              {problem.message}
            </Typography.Text>
          ))}
          {row.problems.length > 2 ? (
            <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
              另有 {row.problems.length - 2} 项
            </Typography.Text>
          ) : null}
        </Flex>
      ),
    },
    {
      title: "标度",
      key: "scale",
      width: 74,
      align: "center",
      render: (_value, row) => (row.defect.defect_scale ? `${row.defect.defect_scale}级` : "—"),
    },
    { title: "照片", key: "photos", width: 170, render: (_value, row) => photosCell(row) },
    {
      // 匹配文案带规范病害名，窄栏会省略；完整内容交给悬停。
      title: "状态",
      key: "status",
      width: 132,
      render: (_value, row) => (
        <Tag color={statusTagColor(row)} variant="filled" title={statusText(row)}>{statusText(row)}</Tag>
      ),
    },
    {
      title: "操作",
      key: "actions",
      width: 108,
      render: (_value, row) => (
        <Flex gap={4} style={{ whiteSpace: "nowrap" }}>
          <Button type="link" size="small" onClick={() => onOpen(row.candidateId)}>查看</Button>
          <Button type="link" size="small" onClick={() => onOpen(row.candidateId)}>编辑</Button>
        </Flex>
      ),
    },
  ];

  if (rows.length === 0) {
    return <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="当前筛选下没有病害。" />;
  }

  /* 拆分态下这一栏只有 ~350px：表格塞不下七列，改成一行一条的紧凑列表；
     页码也退成简洁分页，否则页码 + 每页条数 + 跳页一行放不下。
     高度由外层给定：条目区在里面滚，分页钉在底部。 */
  if (compact) {
    return (
      <Flex vertical gap={8} style={{ flex: 1, minHeight: 0 }}>
        <Flex
          ref={compactItemsRef}
          vertical
          gap={2}
          role="list"
          aria-label="病害列表"
          style={{ flex: 1, minHeight: 0, overflowY: "auto" }}
        >
          {pageRows.map((row) => {
            const active = row.candidateId === activeCandidateId;
            return (
              <Flex
                key={row.candidateId}
                id={reviewTargetId("defect", row.candidateId)}
                data-candidate-id={row.candidateId}
                role="listitem"
                align="flex-start"
                gap={8}
                style={{
                  padding: "6px 8px",
                  borderRadius: token.borderRadius,
                  background: active ? token.colorPrimaryBg : undefined,
                  // 当前项左侧一道主色条：比整圈描边轻，滚动时也一眼找得到。
                  boxShadow: active ? `inset 3px 0 0 ${token.colorPrimary}` : undefined,
                }}
              >
                {selectable ? <div style={{ paddingTop: 4 }}>{selectCell(row)}</div> : null}
                <Button
                  type="text"
                  block
                  onClick={() => onOpen(row.candidateId)}
                  style={{ height: "auto", padding: 0, textAlign: "start", whiteSpace: "normal", background: "transparent" }}
                >
                  <Flex vertical gap={1} style={{ width: "100%", minWidth: 0 }}>
                    <Flex align="center" justify="space-between" gap={6}>
                      <Typography.Text strong ellipsis style={active ? { color: token.colorPrimary } : undefined}>
                        {row.defect.component_number ?? row.defect.component_name}
                      </Typography.Text>
                      <Tag color={statusTagColor(row)} variant="filled" title={statusText(row)} style={{ marginInlineEnd: 0, flex: "none" }}>
                        {statusText(row)}
                      </Tag>
                    </Flex>
                    {displayDefectLocation(row.defect.defect_location) ? (
                      <Typography.Text type="secondary" ellipsis style={{ fontSize: token.fontSizeSM }}>
                        {displayDefectLocation(row.defect.defect_location)}
                      </Typography.Text>
                    ) : null}
                    <Typography.Text ellipsis style={{ fontSize: token.fontSizeSM }}>
                      {ratingTreeDisplayLabel(row.ratingTreeNode) ?? "未确定规范病害"}
                    </Typography.Text>
                    {row.problems.length > 0 ? (
                      <Typography.Text type="warning" ellipsis style={{ fontSize: token.fontSizeSM }}>
                        {row.problems[0].message}{row.problems.length > 1 ? ` 等 ${row.problems.length} 项` : ""}
                      </Typography.Text>
                    ) : null}
                  </Flex>
                </Button>
              </Flex>
            );
          })}
        </Flex>
        <Flex justify="center" style={{ flex: "none" }}>
          <Pagination
            current={currentPage}
            pageSize={pageSize}
            total={rows.length}
            size="small"
            simple={{ readOnly: true }}
            // 窄栏放不下每页条数；这里也没接改条数的回调，给了就是个坏按钮。
            showSizeChanger={false}
            onChange={(nextPage) => {
              onPageChange?.();
              setPage(nextPage);
            }}
          />
        </Flex>
      </Flex>
    );
  }

  return (
    <Table<DefectReviewRow>
      rowKey="candidateId"
      size="small"
      columns={columns}
      dataSource={pageRows}
      scroll={{ x: 900 }}
      onRow={(row) => ({
        id: reviewTargetId("defect", row.candidateId),
        style: row.candidateId === activeCandidateId ? { background: token.colorPrimaryBg } : undefined,
      })}
      pagination={{
        current: currentPage,
        pageSize,
        total: rows.length,
        pageSizeOptions: [20, 50, 100],
        showSizeChanger: true,
        showQuickJumper: true,
        onChange: (nextPage, nextPageSize) => {
          onPageChange?.();
          setPageSize(nextPageSize);
          setPage(nextPageSize === pageSize ? nextPage : 1);
        },
      }}
    />
  );
}
