import { useState } from "react";

import type {
  TriageManualCluster,
  TriageManualObservation,
  TriageResolvePayload,
} from "../api/threadTriageApi";
import { needsInexactMergeConfirmation } from "../api/threadTriageApi";

/**
 * 异常簇卡片：**同屏**展示一簇里全部的组、位置与历年观测。
 *
 * 这里的界面形状是被问题本身逼出来的。系统之所以不敢自动合并"梁底"和"梁底部"，正是
 * 因为它没法判断这两处是不是同一道裂缝；而人要作出判断，就得同时看见这两组各自哪几年
 * 有记录、量测怎么变。一旦退化成"一观测一张卡"，判断所需的上下文恰好被拆没了——旧
 * 整理页就是这么失败的。
 *
 * 因此这张卡不问"这条观测归哪儿"，而是让人**勾选哪些观测属于同一处病害**，再一次性
 * 落成一条线索。合并（全勾）与拆分（分次勾）用的是同一个动作，因为服务端 resolve 接口
 * 本来就是这个形状：给一组观测 + 人选定的类型与位置。
 */
interface ManualClusterCardProps {
  cluster: TriageManualCluster;
  busy: boolean;
  onResolve: (payload: TriageResolvePayload) => void;
  onSkip: () => void;
}

const REASON_LABELS: Record<string, string> = {
  location_overlap: "位置写法可能指同一处",
  multiple_in_year: "同一年有多条记录",
  ambiguous_thread: "同时命中多条已有线索",
};

function reasonLabel(code: string): string {
  return REASON_LABELS[code] ?? code;
}

function locationText(location: string | null): string {
  // 空位置是合法取值而非缺漏：整座构件的通病本来就没有具体位置。
  return location && location.trim().length > 0 ? location : "（无位置）";
}

function observationKey(observation: TriageManualObservation): string {
  return observation.id;
}

export function ManualClusterCard({
  cluster, busy, onResolve, onSkip,
}: ManualClusterCardProps) {
  const allObservations = cluster.groups.flatMap((group) => group.observations);
  const [selectedIds, setSelectedIds] = useState<Set<string>>(
    () => new Set(allObservations.map(observationKey)));
  const [mode, setMode] = useState<"create" | "bind">(
    cluster.related_threads.length > 0 ? "bind" : "create");
  const [targetThreadId, setTargetThreadId] = useState<string>(
    cluster.related_threads[0]?.id ?? "");
  const newest = [...allObservations].sort(
    (left, right) => right.inspection_year - left.inspection_year)[0];
  const [defectType, setDefectType] = useState(newest?.defect_type ?? "");
  const [defectLocation, setDefectLocation] = useState(newest?.defect_location ?? "");
  const [confirmInexact, setConfirmInexact] = useState(false);

  const years = [...new Set(allObservations.map((item) => item.inspection_year))]
    .sort((left, right) => left - right);
  const selected = allObservations.filter(
    (observation) => selectedIds.has(observation.id));
  // 一簇里的组一定同属一个构件（聚簇本来就是按构件做的），线索也只能落在这个构件上。
  const bridgeComponentId = cluster.groups[0]?.bridge_component_id ?? "";
  const inexact = needsInexactMergeConfirmation(selected);
  const blocked = selected.length === 0
    || (mode === "create" && defectType.trim().length === 0)
    || (mode === "create" && inexact && !confirmInexact)
    || (mode === "bind" && targetThreadId.length === 0);

  function toggle(observationId: string): void {
    setSelectedIds((current) => {
      const next = new Set(current);
      if (next.has(observationId)) next.delete(observationId);
      else next.add(observationId);
      return next;
    });
  }

  function submit(): void {
    onResolve({
      action: mode,
      bridge_component_id: bridgeComponentId,
      ...(mode === "create"
        ? {
            defect_type: defectType.trim(),
            defect_location: defectLocation.trim(),
            ...(inexact ? { confirm_inexact_merge: true } : {}),
          }
        : { target_thread_id: targetThreadId }),
      observations: selected.map((observation) => ({
        id: observation.id,
        updated_at: observation.updated_at,
      })),
    });
  }

  return (
    <section
      className="triage-cluster-card"
      aria-label={`异常簇 ${cluster.groups[0]?.business_component_code ?? cluster.cluster_id}`}
    >
      <header className="triage-cluster-head">
        <strong>{cluster.groups[0]?.business_component_code ?? "未知构件"}</strong>
        <span className="triage-cluster-reasons">
          {cluster.reason_codes.map(reasonLabel).join(" · ")}
        </span>
      </header>

      {/* 组、观测两个口径分开说：10 组不等于 10 条，混着写会让人以为工作量小一半。 */}
      <p className="triage-cluster-scale">
        {cluster.group_count} 组 · {cluster.observation_count} 条观测
      </p>

      <table className="data-table triage-cluster-table" aria-label="簇内各位置历年观测">
        <thead>
          <tr>
            <th scope="col">位置写法</th>
            {years.map((year) => <th key={year} scope="col">{year}</th>)}
          </tr>
        </thead>
        <tbody>
          {cluster.groups.map((group) => (
            <tr key={group.group_id}>
              <th scope="row" className="triage-cluster-location">
                {locationText(group.defect_location)}
                <span className="triage-cluster-type">{group.defect_type ?? ""}</span>
              </th>
              {years.map((year) => {
                // 同一年可能有多条——并排放，让人直接比着判断是重复记录还是两处病害。
                const inYear = group.observations.filter(
                  (observation) => observation.inspection_year === year);
                return (
                  <td key={year}>
                    {inYear.length === 0 ? (
                      <span className="triage-cell-empty">—</span>
                    ) : (
                      <div className="triage-cluster-year-cell">
                        {inYear.map((observation, index) => (
                          <label key={observation.id} className="triage-cluster-chip">
                            <input
                              type="checkbox"
                              checked={selectedIds.has(observation.id)}
                              aria-label={
                                `${locationText(group.defect_location)} ${year} 第 ${index + 1} 条`}
                              onChange={() => toggle(observation.id)}
                            />
                            <span>{observation.defect_type}</span>
                          </label>
                        ))}
                      </div>
                    )}
                  </td>
                );
              })}
            </tr>
          ))}
        </tbody>
      </table>

      {cluster.related_threads.length > 0 ? (
        <div className="triage-cluster-threads">
          <h4>该构件上已有的线索</h4>
          <ul>
            {cluster.related_threads.map((thread) => (
              <li key={thread.id}>
                {/* 编号是人在报告里引用线索的唯一凭据，必须显示。 */}
                <span className="triage-thread-number">{thread.system_number}</span>
                <span className="triage-thread-name">{thread.thread_name}</span>
              </li>
            ))}
          </ul>
        </div>
      ) : null}

      <div className="triage-cluster-form">
        <fieldset>
          <legend>把勾选的 {selected.length} 条观测</legend>
          <label>
            <input
              type="radio"
              name={`mode-${cluster.cluster_id}`}
              checked={mode === "create"}
              onChange={() => setMode("create")}
            />
            合并为一条新线索
          </label>
          <label>
            <input
              type="radio"
              name={`mode-${cluster.cluster_id}`}
              checked={mode === "bind"}
              disabled={cluster.related_threads.length === 0}
              onChange={() => setMode("bind")}
            />
            绑定到已有线索
          </label>
        </fieldset>

        {mode === "create" ? (
          <div className="triage-cluster-fields">
            <label>
              病害类型
              <input
                type="text"
                value={defectType}
                onChange={(event) => setDefectType(event.target.value)}
              />
            </label>
            <label>
              位置（可留空）
              <input
                type="text"
                value={defectLocation}
                onChange={(event) => setDefectLocation(event.target.value)}
              />
            </label>
            {inexact ? (
              // 勾选的写法不止一种，这一步就是人在替系统担下"它们是同一处"的判断。
              <label className="triage-cluster-confirm">
                <input
                  type="checkbox"
                  checked={confirmInexact}
                  onChange={(event) => setConfirmInexact(event.target.checked)}
                />
                勾选的观测位置或类型写法不一致，我确认它们是同一处病害
              </label>
            ) : null}
          </div>
        ) : (
          <label className="triage-cluster-target">
            目标线索
            <select
              value={targetThreadId}
              onChange={(event) => setTargetThreadId(event.target.value)}
            >
              {cluster.related_threads.map((thread) => (
                <option key={thread.id} value={thread.id}>
                  {thread.system_number}｜{thread.thread_name}
                </option>
              ))}
            </select>
          </label>
        )}
      </div>

      <div className="triage-cluster-actions">
        <button type="button" className="primary-button" disabled={busy || blocked} onClick={submit}>
          {mode === "create" ? "建为一条线索" : "绑定到该线索"}
        </button>
        <button type="button" disabled={busy} onClick={onSkip}>暂不处理</button>
      </div>
      {selected.length === 0 ? (
        <p className="triage-cluster-hint">先勾选属于同一处病害的观测。</p>
      ) : null}
      {selected.length < allObservations.length ? (
        <p className="triage-cluster-hint">
          余下 {allObservations.length - selected.length} 条这次不处理，可在本次落库后再来一轮。
        </p>
      ) : null}
    </section>
  );
}
