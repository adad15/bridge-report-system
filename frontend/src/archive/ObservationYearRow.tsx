import { useState } from "react";

import type { ArchiveMeasurement, ArchiveObservation, ObservationEvidence } from "../api/componentArchiveApi";
import { defectPhotoContentUrl, fetchObservationEvidence } from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { backendBaseUrl } from "../config";

interface ObservationYearRowProps {
  observation: ArchiveObservation;
  /** T14：已绑定观测的"重新绑定"入口；未提供时不显示按钮（如修订历史只读视图）。 */
  onRebind?: (observation: ArchiveObservation) => void;
}

export function formatArchiveMeasurementValue(measurement: ArchiveMeasurement): string {
  const prefix = measurement.is_approximate ? "约" : "";
  const unit = measurement.unit ?? "";
  if (
    measurement.value_type === "range" &&
    measurement.minimum_value !== null &&
    measurement.maximum_value !== null
  ) {
    return `（${prefix}${measurement.minimum_value}~${measurement.maximum_value}${unit}）`;
  }
  if (measurement.numeric_value !== null) {
    return `（${prefix}${measurement.numeric_value}${unit}）`;
  }
  return "";
}

// 病害线索卡片内的年度观测行（模块 06 §7.2）：
// 摘要行 = 年份｜标度｜扣分｜尺寸｜照片数；展开后显示年度实际位置原文、
// 尺寸原文与结构化值、照片（受控接口）与来源证据。缺照片/缺标度均正常展示。
export function ObservationYearRow({ observation, onRebind }: ObservationYearRowProps) {
  const [expanded, setExpanded] = useState(false);
  const [evidence, setEvidence] = useState<ObservationEvidence | null>(null);
  const [evidenceError, setEvidenceError] = useState<string | null>(null);
  const [evidenceOpen, setEvidenceOpen] = useState(false);

  async function openEvidence(): Promise<void> {
    setEvidenceOpen(true);
    if (evidence !== null) return;
    try {
      setEvidence(await fetchObservationEvidence(backendBaseUrl, observation.id));
      setEvidenceError(null);
    } catch (error) {
      setEvidenceError(error instanceof ApiError ? error.message : "来源证据暂不可用。");
    }
  }

  const measurementSummary = observation.measurements.length > 0
    ? observation.measurements.map((item) => item.raw_text).join("；")
    : "无尺寸记录";
  const splitOrigin = evidence?.source_raw_cells.range_split_origin;
  const splitSourceNumber =
    splitOrigin && typeof splitOrigin === "object"
      && "source_component_number" in splitOrigin
      && typeof splitOrigin.source_component_number === "string"
      ? splitOrigin.source_component_number : null;
  const splitOperatedAt =
    splitOrigin && typeof splitOrigin === "object"
      && "operated_at" in splitOrigin && typeof splitOrigin.operated_at === "string"
      ? splitOrigin.operated_at : null;

  return (
    <div className="archive-observation">
      <button type="button" className="archive-observation-summary" onClick={() => setExpanded(!expanded)}>
        <strong>{observation.inspection_year}</strong>
        <span>标度 {observation.scale ?? "-"}</span>
        <span className="archive-observation-measurement" title={measurementSummary}>
          {measurementSummary}
        </span>
        <span>照片 {observation.photos.length} 张</span>
        <span className="review-status-badge review-status-neutral">{observation.review_status}</span>
      </button>
      {expanded ? (
        <div className="archive-observation-detail">
          <p>
            <strong>年度实际位置：</strong>
            {observation.defect_location ?? "未记录"}
          </p>
          <p>
            <strong>病害描述：</strong>
            {observation.defect_description}
          </p>
          {observation.measurements.length > 0 ? (
            <ul className="archive-measurement-list">
              {observation.measurements.map((item, index) => (
                <li key={`${item.raw_text}-${index}`}>
                  {item.measurement_type}：{item.raw_text}
                  {formatArchiveMeasurementValue(item)}
                </li>
              ))}
            </ul>
          ) : (
            <p className="archive-empty-hint">该年度没有结构化尺寸。</p>
          )}
          {observation.photos.length > 0 ? (
            <div className="archive-photo-strip">
              {observation.photos.map((photo) => (
                <figure key={photo.id}>
                  <img src={defectPhotoContentUrl(backendBaseUrl, photo.id)} alt={`照片 ${photo.photo_number}`} />
                  <figcaption>
                    {photo.photo_number}
                    {photo.photo_title ? `｜${photo.photo_title}` : ""}
                  </figcaption>
                </figure>
              ))}
            </div>
          ) : (
            <p className="archive-empty-hint">该年度病害没有照片。</p>
          )}
          <div className="archive-observation-actions">
            <button type="button" onClick={() => void openEvidence()}>
              查看来源证据
            </button>
            {onRebind ? (
              <button type="button" onClick={() => onRebind(observation)}>
                重新绑定
              </button>
            ) : null}
            <span className="archive-observation-number">{observation.system_number}</span>
          </div>
        </div>
      ) : null}
      {evidenceOpen ? (
        <div className="modal-backdrop" role="dialog" aria-label="来源证据" onClick={() => setEvidenceOpen(false)}>
          <div className="modal-panel" onClick={(event) => event.stopPropagation()}>
            <h3>来源证据</h3>
            {evidenceError ? <p className="archive-empty-hint">{evidenceError}</p> : null}
            {evidence ? (
              <dl className="archive-evidence-list">
                <dt>来源表</dt>
                <dd>
                  {evidence.source_table_title ?? "-"}
                  {evidence.source_row_number !== null ? `（第 ${evidence.source_row_number} 行）` : ""}
                </dd>
                <dt>导入记录</dt>
                <dd>{evidence.import_record_system_number ?? "-"}</dd>
                <dt>来源文件</dt>
                <dd>
                  {evidence.source_file_system_number ?? "-"}
                  {evidence.source_file_name ? `｜${evidence.source_file_name}` : ""}
                </dd>
                {!evidence.original_word_retained ? <><dt>原始 Word</dt><dd>已按临时文件策略清理，当前证据来自解析快照。</dd></> : null}
                {splitSourceNumber ? (
                  <>
                    <dt>拆分来源</dt>
                    <dd>
                      由 {splitSourceNumber} 拆分
                      {splitOperatedAt ? `（${new Date(splitOperatedAt).toLocaleString()}）` : ""}
                    </dd>
                  </>
                ) : null}
                <dt>原始行</dt>
                <dd>
                  <code>{JSON.stringify(evidence.source_raw_cells)}</code>
                </dd>
              </dl>
            ) : null}
            <button type="button" onClick={() => setEvidenceOpen(false)}>
              关闭
            </button>
          </div>
        </div>
      ) : null}
    </div>
  );
}
