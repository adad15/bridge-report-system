import {
  ApartmentOutlined,
  AuditOutlined,
  FileDoneOutlined,
  HistoryOutlined,
} from "@ant-design/icons";
import { Progress } from "antd";
import { Link } from "react-router-dom";

import type {
  BridgeDefectComparison,
  DefectGroupDelta,
  DefectTypeDelta,
} from "../api/workspaceApi";
import { useBridgeWorkspace } from "../workspace/BridgeWorkspaceShell";
import {
  componentArchivePath,
  inspectionsPath,
  inspectionWorkspacePath,
} from "../workspace/workspaceState";

const show = (value: string | number | null | undefined) => value ?? "—";

interface DefectComparisonCardProps {
  comparison: BridgeDefectComparison;
}

/** 源数据里的病害类型有前后空格，个别年度还录成了 "/"。展示前统一收拾。 */
function defectTypeLabel(defectType: string): string {
  const trimmed = defectType.trim();
  return trimmed === "" || trimmed === "/" || trimmed === "-" ? "未标明类型" : trimmed;
}

/** "横向裂缝 3 条、网状裂缝 1 条"。条数为 0 的类型不出现在该年度的句子里。 */
function describeTypes(types: DefectTypeDelta[], pick: (type: DefectTypeDelta) => number): string {
  return types
    .filter((type) => pick(type) > 0)
    .sort((left, right) => pick(right) - pick(left))
    .map((type) => `${defectTypeLabel(type.defect_type)} ${pick(type)} 条`)
    .join("、");
}

/** 一类构件的两年对比句：先上年、后最新年，最后给差值。 */
function describeGroup(group: DefectGroupDelta, comparison: BridgeDefectComparison): string {
  const previous = group.previous_count > 0
    ? `${comparison.previous_year} 年记录病害 ${group.previous_count} 条，为`
      + `${describeTypes(group.defect_types, (type) => type.previous_count)}`
    : `${comparison.previous_year} 年未记录病害`;
  const latest = group.latest_count > 0
    ? `${comparison.latest_year} 年记录 ${group.latest_count} 条，为`
      + `${describeTypes(group.defect_types, (type) => type.latest_count)}`
    : `${comparison.latest_year} 年未记录病害`;
  const change = group.latest_count - group.previous_count;
  const trend = change > 0 ? `较上年多 ${change} 条`
    : change < 0 ? `较上年少 ${-change} 条` : "与上年持平";
  const scope = group.changed_component_count > 0
    ? `，其中 ${group.changed_component_count} 个构件条数发生变化`
    : "";
  return `${previous}；${latest}。${trend}${scope}。`;
}

/**
 * 最新年度与上一年度的病害对比，按构件类型成文。
 *
 * 替换掉了原来的"年度检测进展"步骤条：那个步骤条读的是有没有正式结论，结论一出就
 * 永远停在"已完成"，每次打开都在重复同一句话。
 *
 * 口径是**条数**不是病害身份：未整理的观测没有跨年线索，`defect_comparisons` 又是
 * 模块 07 的预留面、当前为空。所以行文只说"多了/少了多少条"，不写"新增了 N 处病害"，
 * 也不做"高度集中于某部位"这类判断——那是人写报告时的结论，不是数据本身。
 */
function DefectComparisonCard({ comparison }: DefectComparisonCardProps) {
  if (!comparison.available) {
    return (
      <section className="workspace-card bridge-comparison-card">
        <h2>年度病害对比</h2>
        <p className="bridge-comparison-note">需要两个已确认的年度检测才能对比，当前不足两个。</p>
      </section>
    );
  }

  // 汇总句用 JS 拼而不是写在 JSX 里：JSX 的换行会在中文标点后留下空格（"，  净增"）。
  const net = comparison.latest_observation_count - comparison.previous_observation_count;
  const summary = `合计：${comparison.previous_year} 年记录病害 `
    + `${comparison.previous_observation_count} 条，${comparison.latest_year} 年 `
    + `${comparison.latest_observation_count} 条。`
    + `${comparison.changed_component_count} 个构件条数发生变化，`
    + `其中增加 ${comparison.increased_observation_count} 条、`
    + `减少 ${comparison.decreased_observation_count} 条，`
    + `净${net >= 0 ? "增" : "减"} ${Math.abs(net)} 条；`
    + `${comparison.unchanged_component_count} 个构件与上年持平。`;
  return (
    <section className="workspace-card bridge-comparison-card">
      <header className="bridge-comparison-head">
        <h2>年度病害对比</h2>
        <span className="bridge-comparison-years">
          {comparison.previous_year} 年 → {comparison.latest_year} 年
        </span>
      </header>

      <div className="bridge-comparison-body">
        {comparison.groups.map((group) => (
          <p key={`${group.structure_part}-${group.component_type}`} className="bridge-comparison-line">
            <strong>{group.structure_part}·{group.component_type}</strong>
            <span className="bridge-comparison-scale">（{group.component_count} 个构件）</span>
            ：{describeGroup(group, comparison)}
          </p>
        ))}
      </div>

      <p className="bridge-comparison-summary">{summary}</p>
      <p className="bridge-comparison-note">以上按病害条数统计，不代表逐条病害的对应关系。</p>
    </section>
  );
}

export function BridgeOverviewPage() {
  const { overview } = useBridgeWorkspace();
  const { bridge, latest_inspection: latest } = overview;
  const completeness = [
    { label: "基础信息", complete: Boolean(bridge.system_number && bridge.bridge_name) },
    { label: "年度检测", complete: latest !== null },
    { label: "病害档案", complete: overview.defect_archive.component_count > 0 },
    { label: "待办清理", complete: overview.pending.total_count === 0 },
  ];
  const completenessPercent = Math.round(
    completeness.filter((item) => item.complete).length / completeness.length * 100,
  );

  const metrics = [
    { label: "档案完整度", value: `${completenessPercent}%`, tone: "blue", icon: <FileDoneOutlined /> },
    { label: "待处理资料", value: overview.pending.import_count, tone: "orange", icon: <AuditOutlined /> },
    { label: "病害构件", value: overview.defect_archive.component_count, tone: "green", icon: <ApartmentOutlined /> },
    { label: "跨年病害线索", value: overview.defect_archive.thread_count, tone: "violet", icon: <HistoryOutlined /> },
  ];

  return (
    <div className="bridge-overview-page bridge-overview-dashboard">
      <section className="bridge-dashboard-metrics" aria-label="桥梁档案概况">
        {metrics.map((metric) => (
          <article key={metric.label} className={`bridge-dashboard-metric is-${metric.tone}`}>
            <span className="bridge-dashboard-metric-icon" aria-hidden="true">{metric.icon}</span>
            <div><span>{metric.label}</span><strong>{metric.value}</strong></div>
          </article>
        ))}
      </section>

      <div className="bridge-overview-grid">
        <section className="workspace-card bridge-latest-card">
          <h2>最新技术状况</h2>
          <div className="bridge-latest-empty" aria-live="polite">
            <span className="bridge-latest-illustration" aria-hidden="true"><FileDoneOutlined /><i /></span>
            <h3>{latest ? `${latest.inspection_year} 年度综合评定` : "尚无正式年度结论"}</h3>
            {latest ? (
              <p>综合评分 <strong>{show(latest.overall_score)}</strong> · 综合评定 <strong>{show(latest.overall_grade)}</strong></p>
            ) : (
              <p>完成年度检测并确认后，将在此生成综合评定结论。</p>
            )}
            <Link className="primary-link" to={inspectionsPath(bridge.id)}>进入年度检测</Link>
          </div>
        </section>

        <section className="workspace-card bridge-completeness-card">
          <h2>档案完整度</h2>
          <div className="bridge-completeness-body">
            <Progress type="circle" percent={completenessPercent} size={146} strokeWidth={8} />
            <ul>
              {completeness.map((item) => (
                <li key={item.label} className={item.complete ? "is-complete" : ""}>
                  <span aria-hidden="true">{item.complete ? "✓" : "—"}</span>
                  <strong>{item.label}</strong>
                  <em>{item.complete ? "已完成" : "待完善"}</em>
                </li>
              ))}
            </ul>
          </div>
          <p className="bridge-card-footnote">完善档案信息，提升评定准确性与管理效率。</p>
        </section>

        <DefectComparisonCard comparison={overview.defect_comparison} />

        <section className="workspace-card bridge-activity-card">
          <h2>最近动态</h2>
          {overview.recent_inspections.length > 0 ? (
            <ul>
              {overview.recent_inspections.slice(0, 3).map((item) => (
                <li key={item.id}>
                  <span aria-hidden="true" />
                  <div><Link to={inspectionWorkspacePath(bridge.id, item.id)}>{item.inspection_year} 年度检测</Link><p>{item.status} · V{item.version_number}</p></div>
                  <time>{item.updated_at ? new Date(item.updated_at).toLocaleDateString("zh-CN") : ""}</time>
                </li>
              ))}
            </ul>
          ) : (
            <div className="bridge-activity-empty"><span aria-hidden="true" /><div><strong>桥梁档案已建立</strong><p>新的年度检测与档案更新会记录在这里。</p></div></div>
          )}
          <Link className="bridge-card-more" to={componentArchivePath(bridge.id)}>查看构件病害档案</Link>
        </section>
      </div>
    </div>
  );
}
