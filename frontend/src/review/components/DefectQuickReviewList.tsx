import { useEffect, useMemo, useState } from "react";

import { photoContentUrl } from "../../api/reviewApi";
import type { DefectReviewRow } from "../defectPhotoReviewModel";
import { reviewTargetId } from "../reviewNavigation";
import { ratingTreeDisplayLabel as nodeLabel } from "../../rating-tree/ratingTreeLabels";
import { displayDefectLocation } from "./displayHelpers";

const PAGE_SIZE = 50;

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
  const [page, setPage] = useState(0);
  const pageCount = Math.max(1, Math.ceil(rows.length / PAGE_SIZE));
  const currentPage = Math.min(page, pageCount - 1);

  useEffect(() => {
    if (!activeCandidateId) return;
    const index = rows.findIndex((row) => row.candidateId === activeCandidateId);
    if (index >= 0) setPage(Math.floor(index / PAGE_SIZE));
  }, [activeCandidateId, rows]);

  const pageRows = useMemo(
    () => rows.slice(currentPage * PAGE_SIZE, (currentPage + 1) * PAGE_SIZE),
    [currentPage, rows],
  );

  return (
    <div className={`defect-quick-review ${compact ? "compact" : ""}`}>
      {rows.length === 0 ? <p className="empty-review-result">当前筛选下没有病害。</p> : null}
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
              <small>标度 {row.defect.defect_scale ?? "未填"}</small>
            </span>
            {/* 每行只留一个徽标：已忽略/已确认这类终态直接说终态，其余说匹配结论。
                "可批量确认"由行首那个可勾选的复选框表达，不必再占一个徽标位。 */}
            <span className={`defect-quick-match ${row.matchState}`}>{row.matchLabel}</span>
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
            <span>{row.photos.length} 张</span>
          </div>
        </div>
      ))}
      {pageCount > 1 ? (
        <div className="defect-pagination">
          <button type="button" disabled={currentPage === 0} onClick={() => { onPageChange?.(); setPage(currentPage - 1); }}>上一页</button>
          <span>第 {currentPage + 1} / {pageCount} 页（共 {rows.length} 条）</span>
          <button type="button" disabled={currentPage + 1 >= pageCount} onClick={() => { onPageChange?.(); setPage(currentPage + 1); }}>下一页</button>
        </div>
      ) : null}
    </div>
  );
}
