import { ApiError, request } from "./apiClient";

export type RatingTreeScoringMode = "inherit_h21" | "non_scoring";

export interface RatingTreeSource {
  source_type: "technical_condition" | "maintenance" | "organization" | string;
  source_id?: string;
  source_key?: string;
  package_version?: string;
  content_checksum?: string;
  title?: string;
  reference?: string;
  rule_id?: string | null;
}

export interface RatingTreeVersionSummary {
  id: string;
  tree_code: string;
  tree_name: string;
  package_version: string;
  tree_content_checksum: string;
  status: "published";
  published_at: string;
  h21_package_version?: string;
  maintenance_package_version?: string;
}

export interface RatingTreeVersion extends RatingTreeVersionSummary {
  contract_version: number;
  node_count: number;
  sources: RatingTreeSource[];
}

export interface RatingTreePathItem {
  id: string;
  node_key: string;
  display_name: string;
  node_type: string;
}

export interface RatingTreeNodeSummary {
  id: string;
  node_key: string;
  parent_node_id: string | null;
  display_name: string;
  node_type: string;
  sort_order: number;
  bridge_type_ids: string[];
  component_category_ids: string[];
  scoring_mode: RatingTreeScoringMode;
  h21_indicator_id: string | null;
  is_selectable: boolean;
  is_scoring: boolean;
  path?: RatingTreePathItem[];
}

export interface RatingTreeNode extends RatingTreeNodeSummary {
  organization_note: string;
  allowed_scales: number[];
  h21_indicator_name: string | null;
  h21_source_table: string | null;
  scale_descriptions: Record<string, string>;
  deduction_points: Record<string, number>;
  path: RatingTreePathItem[];
  sources: RatingTreeSource[];
}

const versionCache = new Map<string, Promise<RatingTreeVersion>>();
const nodeListCache = new Map<string, Promise<RatingTreeNodeSummary[]>>();
const nodeDetailCache = new Map<string, Promise<RatingTreeNode>>();

function cached<T>(cache: Map<string, Promise<T>>, key: string, load: () => Promise<T>): Promise<T> {
  const existing = cache.get(key);
  if (existing !== undefined) return existing;
  const pending = load().catch((error) => {
    cache.delete(key);
    throw error;
  });
  cache.set(key, pending);
  return pending;
}

export async function fetchRatingTreeVersions(baseUrl: string): Promise<RatingTreeVersionSummary[]> {
  const body = await request<{ versions: RatingTreeVersionSummary[] }>(`${baseUrl}/api/rating-trees`);
  return body.versions;
}

export function fetchRatingTreeVersion(baseUrl: string, versionId: string): Promise<RatingTreeVersion> {
  const key = `${baseUrl}:${versionId}`;
  return cached(versionCache, key, async () => {
    const body = await request<{ version: RatingTreeVersion }>(
      `${baseUrl}/api/rating-trees/${encodeURIComponent(versionId)}`,
    );
    return body.version;
  });
}

export function fetchRatingTreeChildren(
  baseUrl: string,
  versionId: string,
  parentNodeId: string | null,
): Promise<RatingTreeNodeSummary[]> {
  const parent = parentNodeId ?? "root";
  const key = `${baseUrl}:${versionId}:${parent}`;
  return cached(nodeListCache, key, async () => {
    const params = new URLSearchParams({ parent_id: parent, limit: "200" });
    const body = await request<{ nodes: RatingTreeNodeSummary[] }>(
      `${baseUrl}/api/rating-trees/${encodeURIComponent(versionId)}/nodes?${params}`,
    );
    return body.nodes;
  });
}

export function fetchRatingTreeNode(
  baseUrl: string,
  versionId: string,
  nodeId: string,
): Promise<RatingTreeNode> {
  const key = `${baseUrl}:${versionId}:${nodeId}`;
  return cached(nodeDetailCache, key, async () => {
    const body = await request<{ node: RatingTreeNode }>(
      `${baseUrl}/api/rating-trees/${encodeURIComponent(versionId)}/nodes/${encodeURIComponent(nodeId)}`,
    );
    return body.node;
  });
}

export async function searchRatingTree(
  baseUrl: string,
  versionId: string,
  query: string,
): Promise<RatingTreeNodeSummary[]> {
  const params = new URLSearchParams({ q: query, limit: "200" });
  const body = await request<{ nodes: RatingTreeNodeSummary[] }>(
    `${baseUrl}/api/rating-trees/${encodeURIComponent(versionId)}/search?${params}`,
  );
  return body.nodes;
}

export async function fetchApplicableRatingTreeDefects(
  baseUrl: string,
  versionId: string,
  bridgeTypeId: string,
  componentCategoryId: string,
): Promise<RatingTreeNodeSummary[]> {
  const params = new URLSearchParams({
    bridge_type_id: bridgeTypeId,
    component_category_id: componentCategoryId,
    limit: "200",
  });
  const body = await request<{ nodes: RatingTreeNodeSummary[] }>(
    `${baseUrl}/api/rating-trees/${encodeURIComponent(versionId)}/applicable-defects?${params}`,
  );
  return body.nodes;
}

export function ratingTreeErrorMessage(error: unknown): string {
  if (!(error instanceof ApiError)) return "评定树加载失败，请稍后重试。";
  const stable: Record<string, string> = {
    rating_tree_not_found: "评定树版本不存在或尚未发布。",
    rating_tree_node_not_found: "评定树节点不存在。",
    unauthorized: "登录状态已失效，请重新登录。",
  };
  return stable[error.code] ?? error.message;
}

export function clearRatingTreeApiCacheForTests(): void {
  versionCache.clear();
  nodeListCache.clear();
  nodeDetailCache.clear();
}
