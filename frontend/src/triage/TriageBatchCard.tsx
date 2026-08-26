import type { TriageBatchSummary, TriageSampleGroup } from "../api/threadTriageApi";

/**
 * 批次卡片：**纯展示**。
 *
 * 它不自行请求、不自行写库——旧整理页的每张卡各发一次候选请求，1197 张卡就把浏览器
 * 连接池堵死了。展开、剔除、提交、刷新一律由页面统一管。
 */
interface TriageBatchCardProps {
  batch: TriageBatchSummary;
  expanded: boolean;
  busy: boolean;
  onToggleExpand: () => void;
  onConfirm: () => void;
  onSkip: () => void;
  children?: React.ReactNode;
}

function yearSpan(years: number[]): string {
  if (years.length === 0) return "无年度";
  if (years.length === 1) return `仅 ${years[0]}`;
  return years.join(" → ");
}

function sampleLabel(group: TriageSampleGroup): string {
  // 构件业务编号是人唯一能据以分辨的东西。缺了就退回 group_id 前 8 位——总好过
  // 三张卡片都写着"盖梁"，那正是旧页面从根上没法用的原因。
  return group.business_component_code ?? `构件 ${group.group_id.slice(0, 8)}`;
}

export function TriageBatchCard({
  batch, expanded, busy, onToggleExpand, onConfirm, onSkip, children,
}: TriageBatchCardProps) {
  const location = batch.defect_location ?? "（无位置）";
  return (
    <section className="triage-batch-card" aria-label={`批次 ${batch.component_type} ${batch.defect_type}`}>
      <header className="triage-batch-head">
        <div>
          <strong>
            {batch.structure_part}｜{batch.component_type} · {batch.defect_type} · {location}
          </strong>
          <p className="triage-batch-years">{yearSpan(batch.year_set)}</p>
        </div>
        <span className={`triage-batch-action triage-batch-action-${batch.action}`}>
          {batch.action === "create" ? "批量新建" : "批量绑定"}
        </span>
      </header>

      <p className="triage-batch-scale">
        {batch.group_count} 个构件 · {batch.observation_count} 条观测
      </p>

      {batch.action === "bind" ? (
        // 线索属于具体构件：一个跨多构件的批次会绑到多条不同的线索，批次层面没有唯一编号。
        <p className="triage-batch-bind-note">将分别绑定到各构件中精确命中的已有线索</p>
      ) : null}

      <ul className="triage-batch-samples">
        {batch.sample_groups.map((group) => (
          <li key={group.group_id}>
            <span className="triage-sample-code">{sampleLabel(group)}</span>
            <span className="triage-sample-years">{group.years.join(" · ")}</span>
          </li>
        ))}
        {batch.group_count > batch.sample_groups.length ? (
          <li className="triage-sample-more">
            …还有 {batch.group_count - batch.sample_groups.length} 个
          </li>
        ) : null}
      </ul>

      {expanded ? children : null}

      <div className="triage-batch-actions">
        <button type="button" className="primary-button" disabled={busy} onClick={onConfirm}>
          确认这 {batch.group_count} 组
        </button>
        <button type="button" disabled={busy} onClick={onToggleExpand}>
          {expanded ? "收起" : "展开逐组核对"}
        </button>
        <button type="button" disabled={busy} onClick={onSkip}>暂不处理</button>
      </div>
    </section>
  );
}
