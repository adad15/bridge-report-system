import type { RatingTreeNode, RatingTreeVersion } from "../api/ratingTreeApi";
import type { StandardCatalog } from "../api/standardsApi";

interface RatingTreeNodeDetailProps {
  version: RatingTreeVersion;
  node: RatingTreeNode | null;
  catalog: StandardCatalog | null;
  loading: boolean;
}

const sourceTypeLabel: Record<string, string> = {
  technical_condition: "H21 评分规范",
  maintenance: "JTG 5120 检查养护规范",
  organization: "单位评定规则",
};

function scopeText(
  values: string[],
  names: Map<string, string>,
  allCount: number,
  allLabel: string,
): string {
  if (values.length === 0) return "不限";
  if (allCount > 0 && values.length === allCount) return `${allLabel}（${allCount} 类）`;
  const resolved = values.flatMap((value) => {
    const name = names.get(value);
    return name ? [name] : [];
  });
  if (resolved.length === 0) return `${values.length} 类`;
  if (resolved.length > 6) return `${resolved.slice(0, 6).join("、")}等 ${resolved.length} 类`;
  const unresolvedCount = values.length - resolved.length;
  return `${resolved.join("、")}${unresolvedCount > 0 ? `等 ${values.length} 类` : ""}`;
}

export function RatingTreeNodeDetail({ version, node, catalog, loading }: RatingTreeNodeDetailProps) {
  if (loading) return <div className="rating-tree-detail-state">正在加载节点详情…</div>;
  if (node === null) {
    return (
      <div className="rating-tree-detail-state">
        <strong>{version.tree_name}</strong>
        <span>从左侧选择节点，查看适用范围、规范来源和评分规则。</span>
      </div>
    );
  }

  const bridgeTypeNames = new Map(
    (catalog?.bridge_types ?? []).map((item) => [item.id, item.name]),
  );
  const componentCategoryNames = new Map(
    (catalog?.component_categories ?? []).map((item) => [item.id, item.name]),
  );

  return (
    <article className="rating-tree-detail">
      <header>
        <div className="rating-tree-breadcrumb">
          {node.path.map((item) => item.display_name).join(" / ")}
        </div>
        <div className="rating-tree-detail-title-row">
          <h2>{node.display_name}</h2>
          {node.is_selectable && (
            <span className={node.is_scoring ? "rating-tree-score-badge" : "rating-tree-placeholder-badge"}>
              {node.is_scoring ? "参与评分" : "暂不计分"}
            </span>
          )}
        </div>
      </header>

      <section className="rating-tree-detail-grid">
        <div>
          <span>适用桥型</span>
          <strong>
            {scopeText(
              node.bridge_type_ids,
              bridgeTypeNames,
              catalog?.bridge_types.length ?? 0,
              "全部桥型",
            )}
          </strong>
        </div>
        <div>
          <span>适用构件</span>
          <strong>
            {scopeText(
              node.component_category_ids,
              componentCategoryNames,
              catalog?.component_categories.length ?? 0,
              "全部构件",
            )}
          </strong>
        </div>
        <div>
          <span>评分模式</span>
          <strong>{node.is_scoring ? "继承 H21 评分" : "不参与本期评分"}</strong>
        </div>
        <div>
          <span>H21 指标</span>
          <strong>{node.h21_indicator_name || node.h21_indicator_id || "无"}</strong>
        </div>
      </section>

      {node.organization_note && (
        <section className="rating-tree-detail-section">
          <h3>单位说明</h3>
          <p>{node.organization_note}</p>
        </section>
      )}

      {node.is_scoring ? (
        <section className="rating-tree-detail-section">
          <h3>标度判定与扣分</h3>
          {node.h21_source_table && <p className="rating-tree-source-table">来源：{node.h21_source_table}</p>}
          <div className="rating-tree-scale-table-wrap">
            <table className="rating-tree-scale-table">
              <thead>
                <tr>
                  <th>标度</th>
                  <th>判定说明</th>
                  <th>扣分</th>
                </tr>
              </thead>
              <tbody>
                {node.allowed_scales.map((scale) => (
                  <tr key={scale}>
                    <td>{scale}</td>
                    <td>{node.scale_descriptions[String(scale)] || "—"}</td>
                    <td>{node.deduction_points[String(scale)] ?? "—"}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </section>
      ) : node.is_selectable ? (
        <section className="rating-tree-non-scoring-note">
          该节点用于记录与人工核对，目前不产生扣分。
        </section>
      ) : null}

      <section className="rating-tree-detail-section">
        <h3>规则来源</h3>
        <ul className="rating-tree-sources">
          {node.sources.map((source) => (
            <li key={`${source.source_type}:${source.source_key ?? source.source_id ?? source.reference}`}>
              <strong>{sourceTypeLabel[source.source_type] ?? source.source_type}</strong>
              <span>{source.title ?? source.source_id}</span>
              {source.reference && <small>{source.reference}</small>}
            </li>
          ))}
        </ul>
      </section>
    </article>
  );
}
