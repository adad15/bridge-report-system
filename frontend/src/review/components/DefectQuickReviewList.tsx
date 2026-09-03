import { useEffect, useMemo, useState } from "react";
import { Pagination } from "antd";
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
}: DefectQuickReviewListProps) {
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(DEFAULT_PAGE_SIZE);
  const pageCount = Math.max(1, Math.ceil(rows.length / pageSize));
  const currentPage = Math.min(page, pageCount);

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

  return (
    <div className={`defect-quick-review ${compact ? "compact" : ""}`}>
      {compact ? <h3 className="defect-quick-review-title">病害档案</h3> : null}
      {rows.length === 0 ? <p className="empty-review-result">当前筛选下没有病害。</p> : null}
      {rows.length > 0 && !compact ? (
        <div className="defect-review-table-head" aria-hidden="true">
          <span />
          <span>构件与位置</span>
          <span>病害类型</span>
          <span>标度</span>
          <span>照片</span>
          <span>状态</span>
          <span>操作</span>
        </div>
      ) : null}
      {pageRows.map((row) => (
        <div
          id={reviewTargetId("defect", row.candidateId)}
          key={row.candidateId}
          className={`defect-quick-row ${row.candidateId === activeCandidateId ? "active" : ""}`}
        >
          <label className="defect-quick-select" title={row.batchEligible ? "加入批量确认" : "此病害需人工处理"}>
            <input
              type="checkbox"
              aria-label={`选择病害 ${row.defect.component_number ?? row.candidateId}`}
              disabled={!row.batchEligible}
              checked={selectedCandidateIds.has(row.candidateId)}
              onChange={() => onToggleSelection(row.candidateId)}
            />
          </label>
          <button type="button" className="defect-quick-main" onClick={() => onOpen(row.candidateId)}>
            <span className="defect-quick-component">
              <strong>{row.defect.component_number ?? row.defect.component_name}</strong>
              {/* 位置多为 "/"（Word 里的"无"），空着就不占这一行。 */}
              {displayDefectLocation(row.defect.defect_location) ? (
                <small>{displayDefectLocation(row.defect.defect_location)}</small>
              ) : null}
            </span>
            {/* 规范名与报告原文并列。校对这件事本身就是拿原文核对系统的判断，只给一个
                都不够：只给原文看不出定成了哪种规范病害，只给规范名就没法对着纸质报告
                逐行核。两者相同时不重复显示。 */}
            <span className="defect-quick-defect">
              <strong>{ratingTreeDisplayLabel(row.ratingTreeNode) ?? "未确定规范病害"}</strong>
              {sourceWording(row) ? <small>原文：{sourceWording(row)}</small> : null}
            </span>
            <span className="defect-quick-scale">{row.defect.defect_scale ? `${row.defect.defect_scale}级` : "—"}</span>
            <span className="defect-quick-problems">
              {row.problems.slice(0, 2).map((problem) => (
                <small key={problem.code}>{problem.message}</small>
              ))}
              {row.problems.length > 2 ? <small>另有 {row.problems.length - 2} 项</small> : null}
            </span>
          </button>
          <div className="defect-quick-photos">
            {row.photos.slice(0, 3).map((photo) => (
              <button
                key={photo.candidate_id}
                type="button"
                aria-label={`打开照片 ${photo.photo_number}`}
                onClick={() => onOpen(row.candidateId, photo.candidate_id)}
              >
                <img
                  loading="lazy"
                  src={photoContentUrl(baseUrl, importRecordId, photo.candidate_id)}
                  alt=""
                />
              </button>
            ))}
            {row.photos.length === 0 ? <span className="defect-quick-photo-empty" aria-hidden="true"><PictureOutlined /></span> : null}
            <span>{row.photos.length} 张</span>
          </div>
          {/* 匹配文案带规范病害名，窄栏会省略；完整内容交给悬停。 */}
          {(() => {
            const statusText = row.defect.group_review_status === "已确认"
              ? "已确认"
              : row.status === "ignored"
                ? "已忽略"
                : row.matchLabel;
            return (
              <span
                className={`defect-quick-status ${row.defect.group_review_status === "已确认" ? "confirmed" : row.status}`}
                title={statusText}
              >
                {statusText}
              </span>
            );
          })()}
          <span className="defect-quick-actions">
            <button type="button" onClick={() => onOpen(row.candidateId)}>查看</button>
            <i aria-hidden="true" />
            <button type="button" onClick={() => onOpen(row.candidateId)}>编辑</button>
          </span>
        </div>
      ))}
      {rows.length > 0 ? (
        /* 拆分态下这一栏只有 ~350px：页码 + 每页条数 + 跳页 + 总数一行放不下，
           溢出的宽度会在列表底部拉出一条横向滚动条。窄栏退成简洁分页。 */
        <Pagination
          className="defect-pagination"
          current={currentPage}
          pageSize={pageSize}
          pageSizeOptions={[20, 50, 100]}
          total={rows.length}
          size={compact ? "small" : undefined}
          simple={compact ? { readOnly: true } : undefined}
          showSizeChanger={!compact}
          showQuickJumper={!compact}
          showTotal={compact ? undefined : (total) => `共 ${total} 条`}
          onChange={(nextPage, nextPageSize) => {
            onPageChange?.();
            setPageSize(nextPageSize);
            setPage(nextPageSize === pageSize ? nextPage : 1);
          }}
        />
      ) : null}
    </div>
  );
}
