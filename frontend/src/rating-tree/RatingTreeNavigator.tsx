import { Button, Empty, Flex, Spin, Tag, Tree, Typography, theme, type TreeDataNode } from "antd";
import { useMemo } from "react";

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

function ScoringTag({ node }: { node: RatingTreeNodeSummary }) {
  if (!node.is_selectable) return null;
  return node.is_scoring ? <Tag color="green">计分</Tag> : <Tag color="gold">暂不计分</Tag>;
}

/**
 * 评定树目录。
 *
 * 子节点按需加载：展开时由页面去取下一层，取回来之前这一层先空着。树本身只管显示和
 * 回调，展开、选中、加载中的状态都在页面上，换页回来才能原样恢复。
 */
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
  const { token } = theme.useToken();

  const nodeById = useMemo(() => {
    const map = new Map<string, RatingTreeNodeSummary>();
    for (const node of roots) map.set(node.id, node);
    for (const children of childrenByParent.values()) {
      for (const node of children) map.set(node.id, node);
    }
    return map;
  }, [roots, childrenByParent]);

  const treeData = useMemo(() => {
    const build = (nodes: RatingTreeNodeSummary[]): TreeDataNode[] =>
      nodes.map((node) => {
        const children = childrenByParent.get(node.id);
        return {
          key: node.id,
          // 字符串标题会落到节点的 title 属性上，悬停能看到完整名称。
          title: ratingTreeDisplayLabel(node),
          isLeaf: node.node_type === "defect",
          children: expandedNodeIds.has(node.id) && children ? build(children) : undefined,
        };
      });
    return build(roots);
  }, [roots, childrenByParent, expandedNodeIds]);

  if (searchResults !== null) {
    if (searchResults.length === 0) {
      return <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="没有找到匹配节点。" />;
    }
    return (
      <Flex vertical gap={2} role="list" aria-label="评定树搜索结果">
        {searchResults.map((node) => (
          <div role="listitem" key={node.id}>
            <Button
              type="text"
              block
              onClick={() => onSelect(node)}
              style={{
                height: "auto",
                padding: "6px 10px",
                textAlign: "left",
                background: node.id === selectedNodeId ? token.controlItemBgActive : undefined,
              }}
            >
              <Flex vertical align="start" style={{ width: "100%", minWidth: 0 }}>
                <Typography.Text type="secondary" ellipsis style={{ maxWidth: "100%" }}>
                  {node.path?.map((item) => ratingTreeDisplayLabel(item)).join(" / ") ??
                    ratingTreeDisplayLabel(node)}
                </Typography.Text>
                <Flex align="center" gap={6}>
                  <span>{ratingTreeDisplayLabel(node)}</span>
                  <ScoringTag node={node} />
                </Flex>
              </Flex>
            </Button>
          </div>
        ))}
      </Flex>
    );
  }

  if (roots.length === 0) {
    return <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="当前版本没有可显示的节点。" />;
  }

  return (
    <Tree
      blockNode
      treeData={treeData}
      expandedKeys={[...expandedNodeIds]}
      selectedKeys={selectedNodeId ? [selectedNodeId] : []}
      onExpand={(_keys, { node }) => {
        const summary = nodeById.get(String(node.key));
        if (summary) onToggle(summary);
      }}
      onSelect={(_keys, { node }) => {
        const summary = nodeById.get(String(node.key));
        if (summary) onSelect(summary);
      }}
      titleRender={(data) => {
        const summary = nodeById.get(String(data.key));
        return (
          <Flex align="center" gap={6} component="span">
            <span>{String(data.title)}</span>
            {summary ? <ScoringTag node={summary} /> : null}
            {loadingNodeIds.has(String(data.key)) ? <Spin size="small" /> : null}
          </Flex>
        );
      }}
    />
  );
}
