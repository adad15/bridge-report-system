import { useCallback, useEffect, useMemo, useState } from "react";
import { useNavigate, useParams, useSearchParams } from "react-router-dom";

import {
  fetchRatingTreeChildren,
  fetchRatingTreeNode,
  fetchRatingTreeVersion,
  fetchRatingTreeVersions,
  ratingTreeErrorMessage,
  searchRatingTree,
  type RatingTreeNode,
  type RatingTreeNodeSummary,
  type RatingTreeVersion,
} from "../api/ratingTreeApi";
import {
  fetchStandardCatalog,
  fetchStandardPackages,
  type StandardCatalog,
} from "../api/standardsApi";
import { backendBaseUrl } from "../config";
import { RatingTreeNavigator } from "../rating-tree/RatingTreeNavigator";
import { RatingTreeNodeDetail } from "../rating-tree/RatingTreeNodeDetail";
import {
  readLastRatingTreeVersionId,
  readRatingTreeViewState,
  writeLastRatingTreeVersionId,
  writeRatingTreeViewState,
} from "../rating-tree/ratingTreeViewState";
import "../rating-tree/ratingTreePage.css";

export function RatingTreePage() {
  const { versionId } = useParams<{ versionId: string }>();
  const [searchParams] = useSearchParams();
  const linkedNodeId = searchParams.get("node");
  const navigate = useNavigate();
  const initialState = useMemo(
    () => (versionId ? readRatingTreeViewState(versionId) : null),
    [versionId],
  );
  const [version, setVersion] = useState<RatingTreeVersion | null>(null);
  const [scopeCatalog, setScopeCatalog] = useState<StandardCatalog | null>(null);
  const [roots, setRoots] = useState<RatingTreeNodeSummary[]>([]);
  const [childrenByParent, setChildrenByParent] =
    useState<Map<string, RatingTreeNodeSummary[]>>(new Map());
  const [expandedNodeIds, setExpandedNodeIds] = useState<Set<string>>(
    () => new Set(initialState?.expandedNodeIds ?? []),
  );
  const [selectedNodeId, setSelectedNodeId] = useState<string | null>(
    initialState?.selectedNodeId ?? null,
  );
  const [selectedNode, setSelectedNode] = useState<RatingTreeNode | null>(null);
  const [searchTerm, setSearchTerm] = useState(initialState?.searchTerm ?? "");
  const [searchResults, setSearchResults] = useState<RatingTreeNodeSummary[] | null>(
    initialState?.searchTerm ? [] : null,
  );
  const [loadingNodeIds, setLoadingNodeIds] = useState<Set<string>>(new Set());
  const [detailLoading, setDetailLoading] = useState(false);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (versionId !== undefined) return;
    // 本次会话里已经打开过评定树：直接跳回那个版本，省掉"取版本列表"这一次往返。
    // 顶栏的"评定树"链接永远指向不带版本号的 /rating-trees，来回切页时这一跳
    // 每次都要走一遍。
    const remembered = readLastRatingTreeVersionId();
    if (remembered !== null) {
      navigate(`/rating-trees/${encodeURIComponent(remembered)}`, { replace: true });
      return;
    }
    let active = true;
    void fetchRatingTreeVersions(backendBaseUrl)
      .then((versions) => {
        if (!active) return;
        if (versions.length === 0) {
          setError("当前没有已发布的评定树。");
          setLoading(false);
          return;
        }
        const defaultVersion = versions.find((item) => item.is_default) ?? versions[0];
        navigate(`/rating-trees/${encodeURIComponent(defaultVersion.id)}`, { replace: true });
      })
      .catch((caught) => {
        if (active) {
          setError(ratingTreeErrorMessage(caught));
          setLoading(false);
        }
      });
    return () => {
      active = false;
    };
  }, [navigate, versionId]);

  useEffect(() => {
    if (!versionId) return;
    const restored = readRatingTreeViewState(versionId);
    const restoredExpanded = new Set(restored.expandedNodeIds);
    let active = true;

    setLoading(true);
    setError(null);
    setVersion(null);
    setRoots([]);
    setChildrenByParent(new Map());
    setExpandedNodeIds(restoredExpanded);
    setSelectedNodeId(linkedNodeId || restored.selectedNodeId);
    setSelectedNode(null);
    setSearchTerm(restored.searchTerm);
    setSearchResults(restored.searchTerm ? [] : null);

    // 首屏渲染之后再补上次展开的子树。这段是按层递归拉的，每一层一个往返，
    // 挂在首屏前面会让"回到评定树"越用越慢——上次展开得越深，白屏越久。
    // 每拉到一层就并进状态，树逐层长出来；期间对应节点挂在 loadingNodeIds 上，
    // 用的是展开节点本来就有的那个加载态。
    async function hydrateExpanded(
      nodes: RatingTreeNodeSummary[],
      expanded: Set<string>,
    ): Promise<void> {
      const parents = nodes.filter(
        (node) => expanded.has(node.id) && node.node_type !== "defect",
      );
      if (parents.length === 0) return;
      const parentIds = parents.map((parent) => parent.id);
      setLoadingNodeIds((current) => {
        const next = new Set(current);
        for (const id of parentIds) next.add(id);
        return next;
      });
      try {
        await Promise.all(parents.map(async (parent) => {
          const children = await fetchRatingTreeChildren(backendBaseUrl, versionId!, parent.id);
          if (!active) return;
          setChildrenByParent((current) => {
            // 用户在补齐期间自己展开过这个节点，就别拿旧结果盖掉。
            if (current.has(parent.id)) return current;
            return new Map(current).set(parent.id, children);
          });
          await hydrateExpanded(children, expanded);
        }));
      } finally {
        if (active) {
          setLoadingNodeIds((current) => {
            const next = new Set(current);
            for (const id of parentIds) next.delete(id);
            return next;
          });
        }
      }
    }

    void Promise.all([
      fetchRatingTreeVersion(backendBaseUrl, versionId),
      fetchRatingTreeChildren(backendBaseUrl, versionId, null),
    ])
      .then(async ([loadedVersion, loadedRoots]) => {
        const loadedChildren = new Map<string, RatingTreeNodeSummary[]>();
        const technicalRoot =
          loadedRoots.length === 1 && loadedRoots[0].node_type === "root"
            ? loadedRoots[0]
            : null;
        const navigationRoots = technicalRoot === null
          ? loadedRoots
          : await fetchRatingTreeChildren(backendBaseUrl, versionId, technicalRoot.id);
        if (technicalRoot !== null) {
          loadedChildren.set(technicalRoot.id, navigationRoots);
        }

        const initialExpanded = new Set(restoredExpanded);
        let initialSelectedNodeId = linkedNodeId || restored.selectedNodeId;
        if (initialSelectedNodeId === null && navigationRoots.length > 0) {
          const firstNode = navigationRoots[0];
          initialSelectedNodeId = firstNode.id;
          if (firstNode.node_type !== "defect") {
            initialExpanded.add(firstNode.id);
          }
        }
        if (!active) return;
        // 首屏到此为止：根节点列表已经够画出整棵可见的树。
        setVersion(loadedVersion);
        setRoots(loadedRoots);
        setChildrenByParent(loadedChildren);
        setExpandedNodeIds(initialExpanded);
        setSelectedNodeId(initialSelectedNodeId);
        setLoading(false);
        writeLastRatingTreeVersionId(versionId);

        await hydrateExpanded(navigationRoots, initialExpanded);
      })
      .catch((caught) => {
        if (active) {
          // 记住的版本可能已经停用了，清掉它：下次回到 /rating-trees 会重新解析
          // 默认版本，而不是一头撞进同一个错误。
          writeLastRatingTreeVersionId(null);
          setError(ratingTreeErrorMessage(caught));
          setLoading(false);
        }
      });

    return () => {
      active = false;
    };
  }, [linkedNodeId, versionId]);

  useEffect(() => {
    if (!version?.h21_package_version) {
      setScopeCatalog(null);
      return;
    }
    let active = true;
    void fetchStandardPackages(backendBaseUrl)
      .then((packages) => packages.find(
        (item) =>
          item.family === "technical_condition" &&
          item.algorithm_id === "jtg-h21-2011" &&
          item.package_version === version.h21_package_version &&
          item.sync_status === "正常",
      ))
      .then((standardPackage) =>
        standardPackage
          ? fetchStandardCatalog(backendBaseUrl, standardPackage.id)
          : null
      )
      .then((catalog) => {
        if (active) setScopeCatalog(catalog);
      })
      .catch(() => {
        // 评定树本身仍可查看；目录暂不可用时显示范围数量，不回退展示内部 ID。
        if (active) setScopeCatalog(null);
      });
    return () => {
      active = false;
    };
  }, [version?.h21_package_version]);

  useEffect(() => {
    if (!versionId) return;
    writeRatingTreeViewState(versionId, {
      searchTerm,
      expandedNodeIds: [...expandedNodeIds],
      selectedNodeId,
    });
  }, [expandedNodeIds, searchTerm, selectedNodeId, versionId]);

  useEffect(() => {
    if (!versionId || !selectedNodeId) {
      setSelectedNode(null);
      return;
    }
    let active = true;
    setDetailLoading(true);
    void fetchRatingTreeNode(backendBaseUrl, versionId, selectedNodeId)
      .then((node) => {
        if (active) {
          setSelectedNode(node);
          setDetailLoading(false);
        }
      })
      .catch((caught) => {
        if (active) {
          setError(ratingTreeErrorMessage(caught));
          setDetailLoading(false);
        }
      });
    return () => {
      active = false;
    };
  }, [selectedNodeId, versionId]);

  useEffect(() => {
    if (!versionId) return;
    const query = searchTerm.trim();
    if (!query) {
      setSearchResults(null);
      return;
    }
    let active = true;
    const timer = window.setTimeout(() => {
      void searchRatingTree(backendBaseUrl, versionId, query)
        .then((nodes) => {
          if (active) setSearchResults(nodes);
        })
        .catch((caught) => {
          if (active) setError(ratingTreeErrorMessage(caught));
        });
    }, 250);
    return () => {
      active = false;
      window.clearTimeout(timer);
    };
  }, [searchTerm, versionId]);

  const toggleNode = useCallback(async (node: RatingTreeNodeSummary) => {
    if (!versionId) return;
    if (expandedNodeIds.has(node.id)) {
      setExpandedNodeIds((current) => {
        const next = new Set(current);
        next.delete(node.id);
        return next;
      });
      return;
    }

    setExpandedNodeIds((current) => new Set(current).add(node.id));
    if (childrenByParent.has(node.id)) return;
    setLoadingNodeIds((current) => new Set(current).add(node.id));
    try {
      const children = await fetchRatingTreeChildren(backendBaseUrl, versionId, node.id);
      setChildrenByParent((current) => new Map(current).set(node.id, children));
    } catch (caught) {
      setExpandedNodeIds((current) => {
        const next = new Set(current);
        next.delete(node.id);
        return next;
      });
      setError(ratingTreeErrorMessage(caught));
    } finally {
      setLoadingNodeIds((current) => {
        const next = new Set(current);
        next.delete(node.id);
        return next;
      });
    }
  }, [childrenByParent, expandedNodeIds, versionId]);

  const selectNode = useCallback((node: RatingTreeNodeSummary) => {
    setSelectedNodeId(node.id);
    if (node.node_type !== "defect" && !expandedNodeIds.has(node.id)) {
      void toggleNode(node);
    }
  }, [expandedNodeIds, toggleNode]);

  const navigationRoots = useMemo(() => {
    if (roots.length !== 1 || roots[0].node_type !== "root") return roots;
    return childrenByParent.get(roots[0].id) ?? [];
  }, [childrenByParent, roots]);

  const selectedChildren = selectedNode === null
    ? []
    : childrenByParent.get(selectedNode.id) ?? [];

  // 骨架和真正的树共用 .rating-tree-page / .rating-tree-workspace 这套外框类，
  // 加载完成时只有内容换掉，页头、分栏、圆角都在原地——原来这里是一张 status-panel
  // 小卡片，尺寸和树差着一整屏。
  if (loading) {
    return (
      <section className="rating-tree-page rating-tree-page-skeleton" aria-busy="true">
        <header className="rating-tree-page-header">
          <div>
            <p className="section-kicker">桥梁评定规则</p>
            <span className="rating-tree-skeleton-line rating-tree-skeleton-title" />
            <span className="rating-tree-skeleton-line rating-tree-skeleton-meta" />
          </div>
        </header>
        <div className="rating-tree-workspace">
          <aside className="rating-tree-sidebar">
            <div className="rating-tree-search">
              <span>搜索节点或病害</span>
              <span className="rating-tree-skeleton-line rating-tree-skeleton-input" />
            </div>
            <div className="rating-tree-navigation-scroll">
              <ul className="rating-tree-list">
                {Array.from({ length: 9 }, (_, index) => (
                  <li key={index} className="rating-tree-row">
                    <span className="rating-tree-skeleton-line" />
                  </li>
                ))}
              </ul>
            </div>
          </aside>
          <main className="rating-tree-detail-pane">
            <p className="rating-tree-detail-state">正在加载评定树…</p>
          </main>
        </div>
      </section>
    );
  }
  if (error && version === null) {
    return <section className="status-panel"><p className="error-text">{error}</p></section>;
  }
  if (version === null) return null;

  return (
    <section className="rating-tree-page">
      <header className="rating-tree-page-header">
        <div>
          <p className="section-kicker">桥梁评定规则</p>
          <h1>{version.tree_name}</h1>
          <p>
            版本 {version.package_version} · {version.node_count} 个节点 · 已发布只读
          </p>
        </div>
        <div className="rating-tree-version-chip" title={version.tree_content_checksum}>
          规则版本不可编辑
        </div>
      </header>
      {error && <div className="rating-tree-inline-error">{error}</div>}
      <div className="rating-tree-workspace">
        <aside className="rating-tree-sidebar">
          <label className="rating-tree-search">
            <span>搜索节点或病害</span>
            <input
              type="search"
              value={searchTerm}
              onChange={(event) => setSearchTerm(event.target.value)}
              placeholder="例如：渗水、裂缝、支座"
            />
          </label>
          <div className="rating-tree-navigation-scroll">
            <RatingTreeNavigator
              roots={navigationRoots}
              childrenByParent={childrenByParent}
              expandedNodeIds={expandedNodeIds}
              selectedNodeId={selectedNodeId}
              loadingNodeIds={loadingNodeIds}
              searchResults={searchResults}
              onToggle={(node) => void toggleNode(node)}
              onSelect={selectNode}
            />
          </div>
        </aside>
        <main className="rating-tree-detail-pane">
          <RatingTreeNodeDetail
            version={version}
            node={selectedNode}
            children={selectedChildren}
            catalog={scopeCatalog}
            loading={detailLoading}
            childrenLoading={
              selectedNodeId !== null && loadingNodeIds.has(selectedNodeId)
            }
            onSelectChild={selectNode}
          />
        </main>
      </div>
    </section>
  );
}
