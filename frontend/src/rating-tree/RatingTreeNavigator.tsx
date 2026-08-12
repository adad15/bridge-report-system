import type { RatingTreeNodeSummary } from "../api/ratingTreeApi";
import { ratingTreeDisplayLabel } from "./ratingTreeLabels";

interface RatingTreeNavigatorProps {
  roots: RatingTreeNodeSummary[];
  childrenByParent: ReadonlyMap<string, RatingTreeNodeSummary[]>;
  expandedNodeIds: ReadonlySet<string>;
  selectedNodeId: string | null;
  loadingNodeIds: ReadonlySet<string>;
  searchResults: RatingTreeNodeSummary[] | null;
  onToggle: (node: RatingTreeNodeSummary) => void;
  onSelect: (node: RatingTreeNodeSummary) => void;
}

function isLeaf(node: RatingTreeNodeSummary): boolean {
  return node.node_type === "defect";
}

function nodeLabel(node: RatingTreeNodeSummary) {
  return (
    <>
      <span>{ratingTreeDisplayLabel(node)}</span>
      {node.is_selectable && (
        <span className={node.is_scoring ? "rating-tree-score-badge" : "rating-tree-placeholder-badge"}>
          {node.is_scoring ? "计分" : "暂不计分"}
        </span>
      )}
    </>
  );
}

export function RatingTreeNavigator({
  roots,
  childrenByParent,
  expandedNodeIds,
  selectedNodeId,
  loadingNodeIds,
  searchResults,
  onToggle,
  onSelect,
}: RatingTreeNavigatorProps) {
  if (searchResults !== null) {
    if (searchResults.length === 0) {
      return <p className="rating-tree-empty">没有找到匹配节点。</p>;
    }
    return (
      <ul className="rating-tree-search-results" aria-label="评定树搜索结果">
        {searchResults.map((node) => (
          <li key={node.id}>
            <button
              type="button"
              className={node.id === selectedNodeId ? "rating-tree-search-item selected" : "rating-tree-search-item"}
              onClick={() => onSelect(node)}
            >
              <span className="rating-tree-search-path">
                {node.path?.map((item) => ratingTreeDisplayLabel(item)).join(" / ") ??
                  ratingTreeDisplayLabel(node)}
              </span>
              <span className="rating-tree-search-name">{nodeLabel(node)}</span>
            </button>
          </li>
        ))}
      </ul>
    );
  }

  const renderNodes = (nodes: RatingTreeNodeSummary[], depth: number) => (
    <ul className={depth === 0 ? "rating-tree-list root" : "rating-tree-list"}>
      {nodes.map((node) => {
        const expanded = expandedNodeIds.has(node.id);
        const loading = loadingNodeIds.has(node.id);
        const children = childrenByParent.get(node.id);
        return (
          <li key={node.id}>
            <div
              className={node.id === selectedNodeId ? "rating-tree-row selected" : "rating-tree-row"}
              style={{ paddingInlineStart: `${10 + depth * 18}px` }}
            >
              {isLeaf(node) ? (
                <span className="rating-tree-leaf-marker" aria-hidden="true">•</span>
              ) : (
                <button
                  type="button"
                  className="rating-tree-toggle"
                  aria-label={expanded ? `收起${node.display_name}` : `展开${node.display_name}`}
                  aria-expanded={expanded}
                  onClick={() => onToggle(node)}
                >
                  {loading ? "…" : expanded ? "▾" : "▸"}
                </button>
              )}
              <button type="button" className="rating-tree-node-button" onClick={() => onSelect(node)}>
                {nodeLabel(node)}
              </button>
            </div>
            {expanded && children !== undefined && renderNodes(children, depth + 1)}
          </li>
        );
      })}
    </ul>
  );

  return roots.length === 0
    ? <p className="rating-tree-empty">当前版本没有可显示的节点。</p>
    : renderNodes(roots, 0);
}
