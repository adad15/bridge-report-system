import { Button, Modal, Table } from "antd";
import type { ColumnsType } from "antd/es/table";
import { useState } from "react";

import type { ArchiveObservation, ObservationEvidence } from "../api/componentArchiveApi";
import { defectPhotoContentUrl, fetchObservationEvidence } from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { backendBaseUrl } from "../config";
import { formatArchiveMeasurementValue } from "./observationFormatting";

interface ObservationTableProps {
  observations: ArchiveObservation[];
  /** T14：已绑定观测的"重新绑定"入口；未提供时不显示按钮（如修订历史只读视图）。 */
  onRebind?: (observation: ArchiveObservation) => void;
}

// 年度观测表（模块 06 §7.2）：年度｜病害类型｜位置｜标度｜照片｜操作。
// 展开行给出年度实际位置原文、尺寸原文与结构化值、照片（受控接口）与重绑入口；
// "查看证据"直接打开来源证据。缺照片/缺标度均正常展示。
export function ObservationTable({ observations, onRebind }: ObservationTableProps) {
  const [evidenceTarget, setEvidenceTarget] = useState<ArchiveObservation | null>(null);

  const columns: ColumnsType<ArchiveObservation> = [
    {
      title: "年度",
      dataIndex: "inspection_year",
      key: "inspection_year",
      align: "center",
      width: "10%",
    },
    {
      title: "病害类型",
      dataIndex: "defect_type",
      key: "defect_type",
      align: "center",
      width: "18%",
      ellipsis: true,
    },
    {
      title: "位置",
      key: "defect_location",
      align: "center",
      width: "18%",
      ellipsis: true,
      render: (_value, observation) => observation.defect_location || "未记录位置",
    },
    {
      title: "标度",
      key: "scale",
      align: "center",
      width: "9%",
      render: (_value, observation) => observation.scale ?? "-",
    },
    {
      title: "尺寸",
      key: "measurements",
      align: "center",
      width: "28%",
      // 展示尺寸原文；结构化后的数值在展开行里，这里过长时省略并给出完整 title。
      ellipsis: true,
      render: (_value, observation) =>
        observation.measurements.length > 0
          ? observation.measurements.map((item) => item.raw_text).join("；")
          : "-",
    },
    {
      title: "操作",
      key: "actions",
      align: "center",
      width: "17%",
      render: (_value, observation) => (
        <Button
          type="link"
          size="small"
          className="archive-observation-evidence-link"
          onClick={(event) => {
            // 行点击用于展开详情，证据入口不应顺带触发展开。
            event.stopPropagation();
            setEvidenceTarget(observation);
          }}
        >
          查看证据
        </Button>
      ),
    },
  ];

  return (
    <>
      <Table<ArchiveObservation>
        className="archive-observation-table"
        rowKey={(observation) => observation.id}
        columns={columns}
        dataSource={observations}
        pagination={false}
        size="middle"
        bordered
        expandable={{
          // 渲染稿的表格只有 6 列，展开由整行点击承担，不额外占一列。
          showExpandColumn: false,
          expandRowByClick: true,
          expandedRowClassName: () => "archive-observation-detail-row",
          expandedRowRender: (observation) => (
            <ObservationDetail observation={observation} onRebind={onRebind} />
          ),
        }}
      />
      <ObservationEvidenceModal
        observation={evidenceTarget}
        onClose={() => setEvidenceTarget(null)}
      />
    </>
  );
}

interface ObservationDetailProps {
  observation: ArchiveObservation;
  onRebind?: (observation: ArchiveObservation) => void;
}

// 展开后的年度观测明细：位置原文、病害描述、结构化尺寸、照片与重绑入口。
function ObservationDetail({ observation, onRebind }: ObservationDetailProps) {
  return (
    <div className="archive-observation-detail">
      <p>
        <strong>年度实际位置：</strong>
        {observation.defect_location || "未记录"}
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
        {onRebind ? (
          <Button size="small" onClick={() => onRebind(observation)}>
            重新绑定
          </Button>
        ) : null}
        <span className="archive-observation-number">{observation.system_number}</span>
      </div>
    </div>
  );
}

interface ObservationEvidenceModalProps {
  observation: ArchiveObservation | null;
  onClose: () => void;
}

// 来源证据浮层：来源表 / 导入记录 / 来源文件 / 拆分来源 / 原始行。
function ObservationEvidenceModal({ observation, onClose }: ObservationEvidenceModalProps) {
  const [evidence, setEvidence] = useState<ObservationEvidence | null>(null);
  const [evidenceError, setEvidenceError] = useState<string | null>(null);
  const [loadedFor, setLoadedFor] = useState<string | null>(null);

  if (observation !== null && loadedFor !== observation.id) {
    // 切换观测时立即丢弃上一条的证据，避免弹窗短暂显示错误来源。
    setLoadedFor(observation.id);
    setEvidence(null);
    setEvidenceError(null);
    void fetchObservationEvidence(backendBaseUrl, observation.id)
      .then((body) => setEvidence(body))
      .catch((error: unknown) =>
        setEvidenceError(error instanceof ApiError ? error.message : "来源证据暂不可用。"),
      );
  }

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
    <Modal
      open={observation !== null}
      title="来源证据"
      centered
      width={640}
      onCancel={onClose}
      footer={<Button onClick={onClose}>关闭</Button>}
    >
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
          {!evidence.original_word_retained ? (
            <>
              <dt>原始 Word</dt>
              <dd>已按临时文件策略清理，当前证据来自解析快照。</dd>
            </>
          ) : null}
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
      ) : evidenceError === null ? (
        <p>正在加载来源证据…</p>
      ) : null}
    </Modal>
  );
}
