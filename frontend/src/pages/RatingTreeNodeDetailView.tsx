import {
  ArrowRightOutlined,
  FileTextOutlined,
  InfoCircleFilled,
} from "@ant-design/icons";
import { Tag } from "antd";

import type {
  RatingTreeNode,
  RatingTreeNodeSummary,
  RatingTreeVersion,
} from "../api/ratingTreeApi";
import type { StandardCatalog } from "../api/standardsApi";
import { ratingTreeDisplayLabel, ratingTreeSectionNumber } from "../rating-tree/ratingTreeLabels";

interface RatingTreeNodeDetailViewProps {
  version: RatingTreeVersion;
  node: RatingTreeNode | null;
  children: RatingTreeNodeSummary[];
  catalog: StandardCatalog | null;
  loading: boolean;
  childrenLoading: boolean;
  onSelectChild: (node: RatingTreeNodeSummary) => void;
}

function scopeNames(
  values: string[],
  names: Map<string, string>,
): string[] {
  if (values.length === 0) return ["不限"];
  const resolved = values.flatMap((value) => names.get(value) ?? []);
  if (resolved.length === values.length) return resolved;
  if (resolved.length > 0) return [...resolved, `其他 ${values.length - resolved.length} 项`];
  return [`${values.length} 个构件类别`];
}

function nodeTypeLabel(nodeType: string): string {
  const labels: Record<string, string> = {
    root: "规则根节点",
    structure_group: "结构层级",
    component_group: "构件类别",
    defect: "病害节点",
  };
  return labels[nodeType] ?? "评定节点";
}

function scoringLabel(node: RatingTreeNode): string {
  if (!node.is_scoring) return node.is_selectable ? "记录节点" : "目录节点";
  return node.uses_source_scale_descriptions ? "参照 H21 扣分" : "按病害标度扣分";
}

function summarySource(version: RatingTreeVersion, node: RatingTreeNode): string {
  if (node.h21_source_table) return `H21 ${node.h21_source_table}`;
  const source = node.sources[0] ?? version.sources[0];
  const candidate = source?.title ?? source?.reference ?? source?.package_version;
  if (candidate && candidate.length <= 28 && !candidate.includes("/") && !candidate.includes("\\")) return candidate;
  if (version.h21_package_version) return `JTG/T H21 · v${version.h21_package_version}`;
  return "随发布版本固化";
}

function ScaleRulesPanel({ node }: { node: RatingTreeNode }) {
  return (
    <section className="rating-detail-scale-card" aria-label="标度判定与扣分">
      <div className="rating-detail-section-heading">
        <h3>标度判定与扣分</h3>
        <span>{node.allowed_scales.length > 0 ? `${node.allowed_scales.length} 个标度` : "暂无标度"}</span>
      </div>
      <div className="rating-detail-scale-content">
        {node.uses_source_scale_descriptions ? (
          <p className="rating-tree-source-table">判定来源：来源软件 {node.display_number || ""}；扣分参照：H21 {node.h21_source_table || "—"}</p>
        ) : node.h21_source_table ? <p className="rating-tree-source-table">来源：{node.h21_source_table}</p> : null}
        {node.allowed_scales.length === 0 ? (
          <p className="rating-detail-scale-empty">当前病害节点暂无标度扣分规则。</p>
        ) : (
          <div className="rating-tree-scale-table-wrap">
            <table className="rating-tree-scale-table">
              <thead><tr><th>标度</th><th>判定说明</th><th>扣分</th></tr></thead>
              <tbody>{node.allowed_scales.map((scale) => <tr key={scale}><td>{scale}</td><td>{node.scale_descriptions[String(scale)] || "—"}</td><td>{node.deduction_points[String(scale)] ?? "—"}</td></tr>)}</tbody>
            </table>
          </div>
        )}
      </div>
    </section>
  );
}

export function RatingTreeNodeDetailView({
  version,
  node,
  children,
  catalog,
  loading,
  childrenLoading,
  onSelectChild,
}: RatingTreeNodeDetailViewProps) {
  if (loading) return <div className="rating-tree-detail-state">正在加载节点详情…</div>;
  if (node === null) {
    return (
      <div className="rating-tree-detail-state">
        <strong>{version.tree_name}</strong>
        <span>从左侧选择节点，查看适用范围和评分规则。</span>
      </div>
    );
  }

  const bridgeTypeNames = new Map((catalog?.bridge_types ?? []).map((item) => [item.id, item.name]));
  const componentNames = new Map((catalog?.component_categories ?? []).map((item) => [item.id, item.name]));
  const bridgeScopes = scopeNames(node.bridge_type_ids, bridgeTypeNames);
  const componentScopes = scopeNames(node.component_category_ids, componentNames);
  const sectionNumber = ratingTreeSectionNumber(node) ?? "—";
  const childHeading = children.some((child) => child.node_type === "defect") ? "下级病害节点" : "下级评定项目";
  const knownScales = node.allowed_scales.length > 0
    ? node.allowed_scales
    : [...new Set(children.flatMap((child) => child.allowed_scales ?? []))].sort((a, b) => a - b);
  const deductions = Object.values(node.deduction_points).filter((value): value is number => typeof value === "number");
  const maximumDeduction = deductions.length > 0 ? Math.max(...deductions) : null;

  return (
    <article className="rating-tree-detail rating-tree-detail-redesign">
      <header className="rating-detail-hero">
        <div className="rating-tree-breadcrumb">
          {node.path.filter((item) => item.node_type !== "root").map((item) => ratingTreeDisplayLabel(item)).join(" / ") || version.tree_name}
        </div>
        <div className="rating-tree-detail-title-row">
          <h2>{ratingTreeDisplayLabel(node)}</h2>
          <div className="rating-detail-tags">
            <Tag color="blue">{nodeTypeLabel(node.node_type)}</Tag>
            <Tag color={node.is_scoring ? "green" : node.is_selectable ? "gold" : "default"}>
              {node.is_scoring ? "计分节点" : node.is_selectable ? "记录节点" : "目录节点"}
            </Tag>
          </div>
        </div>
      </header>

      <section className="rating-detail-metadata" aria-label="节点元数据">
        <div><span>节点编号</span><strong>{sectionNumber}</strong></div>
        <div><span>节点类型</span><strong>{nodeTypeLabel(node.node_type)}</strong></div>
        <div><span>适用桥型</span><strong>{bridgeScopes.join("、")}</strong></div>
        <div><span>计分方式</span><strong>{scoringLabel(node)}</strong></div>
      </section>

      <section className="rating-detail-scope" aria-label="适用范围">
        <span>适用范围</span>
        <div>{componentScopes.map((scope) => <Tag key={scope} color="blue">{scope}</Tag>)}</div>
      </section>

      <div className="rating-detail-lower-grid">
        {node.node_type === "defect" ? <ScaleRulesPanel node={node} /> : (
          <section className="rating-detail-children" aria-label={childHeading}>
            <div className="rating-detail-section-heading">
              <h3>{childHeading}</h3>
              <span>{childrenLoading ? "正在加载…" : `${children.length} 项`}</span>
            </div>
            {!childrenLoading && children.length === 0 ? (
              <p className="rating-tree-child-empty">该节点没有下级项目。</p>
            ) : (
              <ul className="rating-tree-child-list rating-detail-child-list">
                {children.map((child) => (
                  <li key={child.id}>
                    <button type="button" aria-label={ratingTreeDisplayLabel(child)} onClick={() => onSelectChild(child)}>
                      <FileTextOutlined aria-hidden="true" />
                      <span className="rating-detail-child-name">{child.display_name}</span>
                      <span className="rating-detail-child-number">{ratingTreeSectionNumber(child) ?? "—"}</span>
                      {child.is_scoring ? <Tag color="green">计分节点</Tag> : null}
                      <ArrowRightOutlined aria-hidden="true" />
                    </button>
                  </li>
                ))}
              </ul>
            )}
          </section>
        )}

        <aside className="rating-detail-summary-column">
          <section className="rating-detail-summary">
            <div className="rating-detail-section-heading"><h3>评分规则摘要</h3></div>
            <dl>
              <div><dt>标度</dt><dd>{knownScales.length > 0 ? `${knownScales[0]}–${knownScales[knownScales.length - 1]}` : "—"}</dd></div>
              <div><dt>最高扣分</dt><dd>{maximumDeduction ?? "—"}</dd></div>
              <div><dt>规则来源</dt><dd>{summarySource(version, node)}</dd></div>
            </dl>
          </section>
          <div className="rating-detail-readonly-note"><InfoCircleFilled /><span>该版本已发布，仅供查看，不能在此页面修改规则。</span></div>
        </aside>
      </div>
    </article>
  );
}
