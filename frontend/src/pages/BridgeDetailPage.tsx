import { useEffect, useState } from "react";
import { Link, useParams } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import {
  fetchImportRecords,
  fetchInspectionYears,
  ImportRecordSummary,
  InspectionYearSummary,
} from "../api/navigationApi";
import { backendBaseUrl } from "../config";

export function BridgeDetailPage() {
  const { bridgeId } = useParams<{ bridgeId: string }>();

  const [years, setYears] = useState<InspectionYearSummary[] | null>(null);
  const [yearsError, setYearsError] = useState<string | null>(null);

  const [records, setRecords] = useState<ImportRecordSummary[] | null>(null);
  const [recordsError, setRecordsError] = useState<string | null>(null);

  useEffect(() => {
    if (!bridgeId) return;
    let cancelled = false;

    setYears(null);
    setYearsError(null);
    fetchInspectionYears(backendBaseUrl, bridgeId)
      .then((result) => {
        if (cancelled) return;
        setYears(result);
        setYearsError(null);
      })
      .catch((caught: unknown) => {
        if (cancelled) return;
        setYears(null);
        setYearsError(caught instanceof ApiError ? caught.message : "加载年度检测任务失败");
      });

    return () => {
      cancelled = true;
    };
  }, [bridgeId]);

  useEffect(() => {
    if (!bridgeId) return;
    let cancelled = false;

    setRecords(null);
    setRecordsError(null);
    fetchImportRecords(backendBaseUrl, bridgeId)
      .then((result) => {
        if (cancelled) return;
        setRecords(result);
        setRecordsError(null);
      })
      .catch((caught: unknown) => {
        if (cancelled) return;
        setRecords(null);
        setRecordsError(caught instanceof ApiError ? caught.message : "加载导入记录失败");
      });

    return () => {
      cancelled = true;
    };
  }, [bridgeId]);

  if (!bridgeId) {
    return (
      <section className="status-panel">
        <p className="error-text">缺少桥梁编号。</p>
      </section>
    );
  }

  return (
    <>
      <section className="status-panel">
        <h1>构件病害档案</h1>
        <p>
          按病害线索查看构件历年观测、照片与评分校验证据，并整理未绑定观测：
          <Link to={`/bridges/${bridgeId}/components`}>进入构件病害档案</Link>
          ｜
          <Link to={`/bridges/${bridgeId}/defect-threads/review`}>线索整理</Link>
        </p>
      </section>

      <section className="status-panel">
        <h1>年度检测任务</h1>
        {yearsError ? <p className="error-text">{yearsError}</p> : null}
        {!yearsError && years === null ? <p>加载中…</p> : null}
        {years !== null && years.length === 0 ? <p>暂无年度检测任务。</p> : null}
        {years !== null && years.length > 0 ? (
          <table className="data-table">
            <thead>
              <tr>
                <th>系统编号</th>
                <th>年度</th>
                <th>状态</th>
                <th>版本</th>
                <th>当前</th>
              </tr>
            </thead>
            <tbody>
              {years.map((year) => (
                <tr key={year.id}>
                  <td>{year.system_number}</td>
                  <td>{year.inspection_year}</td>
                  <td>{year.status}</td>
                  <td>{year.version_number}</td>
                  <td>{year.is_current ? "是" : "否"}</td>
                </tr>
              ))}
            </tbody>
          </table>
        ) : null}
      </section>

      <section className="status-panel">
        <h1>导入记录</h1>
        {recordsError ? <p className="error-text">{recordsError}</p> : null}
        {!recordsError && records === null ? <p>加载中…</p> : null}
        {records !== null && records.length === 0 ? <p>暂无导入记录。</p> : null}
        {records !== null && records.length > 0 ? (
          <table className="data-table">
            <thead>
              <tr>
                <th>系统编号</th>
                <th>名称</th>
                <th>来源类型</th>
                <th>状态</th>
                <th>编辑状态</th>
                <th>创建时间</th>
                <th>操作</th>
              </tr>
            </thead>
            <tbody>
              {records.map((record) => {
                const inspectionYearSegment = record.inspection_year_id ?? "unassigned";
                const reviewPath = `/bridges/${encodeURIComponent(bridgeId)}/inspections/${encodeURIComponent(
                  inspectionYearSegment
                )}/imports/${encodeURIComponent(record.id)}/review`;
                return (
                  <tr key={record.id}>
                    <td>{record.system_number}</td>
                    <td>{record.import_name}</td>
                    <td>{record.source_type}</td>
                    <td>{record.import_status}</td>
                    <td>
                      {record.edit_lock
                        ? `${record.edit_lock.owner_display_name} 正在编辑（${record.edit_lock.acquired_at}）`
                        : "—"}
                    </td>
                    <td>{record.created_at}</td>
                    <td>
                      <Link to={reviewPath}>{record.import_status === "待校对" ? "进入校对" : "查看结果"}</Link>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        ) : null}
      </section>
    </>
  );
}
