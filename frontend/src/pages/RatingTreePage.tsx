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
import { backendBaseUrl } from "../config";
import { RatingTreeNavigator } from "../rating-tree/RatingTreeNavigator";
import { RatingTreeNodeDetail } from "../rating-tree/RatingTreeNodeDetail";
import {
  readRatingTreeViewState,
  writeRatingTreeViewState,
} from "../rating-tree/ratingTreeViewState";

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
    let active = true;
    void fetchRatingTreeVersions(backendBaseUrl)
      .then((versions) => {
        if (!active) return;
        if (versions.length === 0) {
          setError("当前没有已发布的评定树。");
          setLoading(false);
          return;
        }
        navigate(`/rating-trees/${encodeURIComponent(versions[0].id)}`, { replace: true });
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

    async function hydrateExpanded(
      nodes: RatingTreeNodeSummary[],
      loaded: Map<string, RatingTreeNodeSummary[]>,
    ): Promise<void> {
      const parents = nodes.filter(
        (node) => restoredExpanded.has(node.id) && node.node_type !== "defect",
      );
      await Promise.all(parents.map(async (parent) => {
        const children = await fetchRatingTreeChildren(backendBaseUrl, versionId!, parent.id);
        loaded.set(parent.id, children);
        await hydrateExpanded(children, loaded);
      }));
    }

    void Promise.all([
      fetchRatingTreeVersion(backendBaseUrl, versionId),
      fetchRatingTreeChildren(backendBaseUrl, versionId, null),
    ])
      .then(async ([loadedVersion, loadedRoots]) => {
        const loadedChildren = new Map<string, RatingTreeNodeSummary[]>();
        await hydrateExpanded(loadedRoots, loadedChildren);
        if (!active) return;
        setVersion(loadedVersion);
        setRoots(loadedRoots);
        setChildrenByParent(loadedChildren);
        setLoading(false);
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
  }, [linkedNodeId, versionId]);

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

  if (loading) {
    return <section className="status-panel"><p>首次加载评定树…</p></section>;
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
              roots={roots}
              childrenByParent={childrenByParent}
              expandedNodeIds={expandedNodeIds}
              selectedNodeId={selectedNodeId}
              loadingNodeIds={loadingNodeIds}
              searchResults={searchResults}
              onToggle={(node) => void toggleNode(node)}
              onSelect={(node) => setSelectedNodeId(node.id)}
            />
          </div>
        </aside>
        <main className="rating-tree-detail-pane">
          <RatingTreeNodeDetail
            version={version}
            node={selectedNode}
            loading={detailLoading}
          />
        </main>
      </div>
    </section>
  );
}
