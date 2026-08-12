import { Link } from "react-router-dom";

import { useBridgeWorkspace } from "../workspace/BridgeWorkspaceShell";
import {
  componentArchivePath,
  inspectionsPath,
  inspectionWorkspacePath,
} from "../workspace/workspaceState";

const show = (value: string | number | null | undefined) => value ?? "—";

// 待办项有值才需要人动手，蓝底加点把注意力引过去；归零就退成灰。
const pendingTone = (value: number) => (value > 0 ? "metric-live" : "metric-zero");
// 统计项不是待办，有值也只是普通读数，只在归零时弱化。
const statTone = (value: number) => (value > 0 ? "" : "metric-zero");

export function BridgeOverviewPage() {
  const { overview } = useBridgeWorkspace();
  const { bridge, latest_inspection: latest } = overview;
  return (
    <div className="bridge-overview-page">
      <section className="workspace-card overview-hero">
        <div>
          <p className="section-kicker">最新正式结论</p>
          <h2>{latest ? `${latest.inspection_year} 年度检测` : "尚无正式年度结论"}</h2>
          {latest ? (
            <p>综合评分 <strong>{show(latest.overall_score)}</strong> · 综合评定 <strong>{show(latest.overall_grade)}</strong></p>
          ) : (
            // 原先这里照样印"综合评分 — · 综合评定 —"，一行里两个破折号读不出到底是
            // 没测、还是测了没分。空态直接说清楚，比占位符有用。
            <p className="muted-text">这座桥还没有已确认的年度检测，综合评分与综合评定要等首次检测确认后生成。</p>
          )}
        </div>
        <Link className="primary-link" to={inspectionsPath(bridge.id)}>进入年度检测</Link>
      </section>

      {/* 台账不在总览页加载，也不在此重复入口：工作区标签导航里已有"构件台账"。
          待处理事项和病害档案概况原本是两张卡，但后端 pending.unbound_observation_count
          就是 defect_archive.unbound_observation_count 的赋值（WorkspaceRepository.cpp:141），
          同一个数被印成"病害观测待整理"和"待绑定观测"两件事。并成一排，去掉重复的那个。 */}
      <section className="workspace-card">
        <div className="card-heading">
          <p className="section-kicker">工作面板</p>
          <Link to={componentArchivePath(bridge.id)}>查看构件档案</Link>
        </div>
        <div className="metric-grid metric-grid-four">
          <Link className={pendingTone(overview.pending.import_count)} to={inspectionsPath(bridge.id)}>
            <strong>{overview.pending.import_count}</strong><span>导入资料待处理</span>
          </Link>
          <Link className={pendingTone(overview.defect_archive.unbound_observation_count)} to={componentArchivePath(bridge.id)}>
            <strong>{overview.defect_archive.unbound_observation_count}</strong><span>待绑定观测</span>
          </Link>
          <div className={statTone(overview.defect_archive.component_count)}>
            <strong>{overview.defect_archive.component_count}</strong><span>有病害构件</span>
          </div>
          <div className={statTone(overview.defect_archive.thread_count)}>
            <strong>{overview.defect_archive.thread_count}</strong><span>跨年病害线索</span>
          </div>
        </div>
      </section>

      <section className="workspace-card">
        <p className="section-kicker">历年技术状况</p>
        {overview.recent_inspections.length === 0 ? (
          // 上面的结论卡已经给了"进入年度检测"，这里不再放第二个同址按钮，只说明这块
          // 什么时候会有内容。
          <p className="empty-hint">还没有已确认或已归档的年度。年度检测确认后会出现在这里。</p>
        ) : (
          <table className="data-table">
            <thead><tr><th>年度</th><th className="numeric-cell">综合评分</th><th>等级</th><th>状态</th></tr></thead>
            <tbody>{overview.recent_inspections.map((item) => (
              <tr key={item.id}>
                <td><Link to={inspectionWorkspacePath(bridge.id, item.id)}>{item.inspection_year}</Link></td>
                <td className="numeric-cell">{show(item.overall_score)}</td>
                <td>{show(item.overall_grade)}</td>
                <td>{item.status}</td>
              </tr>
            ))}</tbody>
          </table>
        )}
        {overview.structure_ratings.length > 0 ? (
          <div className="rating-chips">{overview.structure_ratings.map((item) => <span key={`${item.rating_level}-${item.rating_item_name}`}>{item.rating_item_name}：{show(item.score)} {show(item.grade)}</span>)}</div>
        ) : null}
      </section>
    </div>
  );
}
