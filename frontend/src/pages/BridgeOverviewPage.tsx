import { Link } from "react-router-dom";

import { useBridgeWorkspace } from "../workspace/BridgeWorkspaceShell";
import {
  componentArchivePath,
  inspectionsPath,
  inspectionWorkspacePath,
} from "../workspace/workspaceState";

const show = (value: string | number | null | undefined) => value ?? "—";

export function BridgeOverviewPage() {
  const { overview } = useBridgeWorkspace();
  const { bridge, latest_inspection: latest } = overview;
  return (
    <div className="bridge-overview-page">
      <section className="workspace-card overview-hero">
        <div>
          <p className="section-kicker">最新正式结论</p>
          <h2>{latest ? `${latest.inspection_year} 年度检测` : "尚无正式年度结论"}</h2>
          <p>综合评分 <strong>{show(latest?.overall_score)}</strong> · 综合评定 <strong>{show(latest?.overall_grade)}</strong></p>
        </div>
        <Link className="primary-link" to={inspectionsPath(bridge.id)}>进入年度检测</Link>
      </section>

      {/* 台账不在总览页加载，也不在此重复入口：工作区标签导航里已有"构件台账"。 */}
      <section className="workspace-card">
        <div className="card-heading"><div><p className="section-kicker">待处理事项</p><h2>{overview.pending.total_count} 项</h2></div></div>
        <div className="metric-grid">
          <Link to={inspectionsPath(bridge.id)}><strong>{overview.pending.import_count}</strong><span>导入资料待处理</span></Link>
          <Link to={componentArchivePath(bridge.id)}><strong>{overview.pending.unbound_observation_count}</strong><span>病害观测待整理</span></Link>
        </div>
      </section>

      <section className="workspace-card">
        <div className="card-heading"><div><p className="section-kicker">历年技术状况</p><h2>最近有效年度</h2></div></div>
        {overview.recent_inspections.length === 0 ? <p className="empty-hint">暂无已确认或已归档年度。</p> : (
          <table className="data-table"><thead><tr><th>年度</th><th>综合评分</th><th>等级</th><th>状态</th></tr></thead>
            <tbody>{overview.recent_inspections.map((item) => <tr key={item.id}><td><Link to={inspectionWorkspacePath(bridge.id, item.id)}>{item.inspection_year}</Link></td><td>{show(item.overall_score)}</td><td>{show(item.overall_grade)}</td><td>{item.status}</td></tr>)}</tbody>
          </table>
        )}
        {overview.structure_ratings.length > 0 ? (
          <div className="rating-chips">{overview.structure_ratings.map((item) => <span key={`${item.rating_level}-${item.rating_item_name}`}>{item.rating_item_name}：{show(item.score)} {show(item.grade)}</span>)}</div>
        ) : null}
      </section>

      <section className="workspace-card">
        <div className="card-heading"><div><p className="section-kicker">病害档案概况</p><h2>按构件持续跟踪</h2></div><Link to={componentArchivePath(bridge.id)}>查看构件档案</Link></div>
        <div className="metric-grid metric-grid-three">
          <div><strong>{overview.defect_archive.component_count}</strong><span>有病害构件</span></div>
          <div><strong>{overview.defect_archive.thread_count}</strong><span>跨年病害线索</span></div>
          <div><strong>{overview.defect_archive.unbound_observation_count}</strong><span>待绑定观测</span></div>
        </div>
      </section>
    </div>
  );
}
