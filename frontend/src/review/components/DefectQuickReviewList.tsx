import { useEffect, useMemo, useState } from "react";

import { photoContentUrl } from "../../api/reviewApi";
import type { DefectReviewRow } from "../defectPhotoReviewModel";
import { reviewTargetId } from "../reviewNavigation";

const PAGE_SIZE = 50;

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

const STATUS_LABELS: Record<DefectReviewRow["status"], string> = {
  batchable: "可批量确认",
  needs_attention: "需处理",
  confirmed: "已确认",
  ignored: "已忽略",
};

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
              <small>{row.defect.defect_location}</small>
            </span>
            <span className="defect-quick-defect">
              <strong>{row.indicator?.name ?? (row.defect.defect_type || "未确定规范病害")}</strong>
              <small>标度 {row.defect.defect_scale ?? "未填"}</small>
            </span>
            <span className={`defect-quick-status ${row.status}`}>{STATUS_LABELS[row.status]}</span>
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
