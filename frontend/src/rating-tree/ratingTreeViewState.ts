export interface RatingTreeViewState {
  searchTerm: string;
  expandedNodeIds: string[];
  selectedNodeId: string | null;
}

const stateByVersion = new Map<string, RatingTreeViewState>();

// 本次会话最后一次成功打开的版本。/rating-trees 不带版本号进来时用它直接跳转，
// 省掉"取版本列表"那一次往返。只活在内存里：刷新即清空，所以不会留下指向已下架
// 版本的陈旧记忆；万一版本在会话中途被停用，加载失败时会把它清掉重新解析。
let lastVersionId: string | null = null;

const emptyState = (): RatingTreeViewState => ({
  searchTerm: "",
  expandedNodeIds: [],
  selectedNodeId: null,
});

export function readRatingTreeViewState(versionId: string): RatingTreeViewState {
  const state = stateByVersion.get(versionId) ?? emptyState();
  return {
    ...state,
    expandedNodeIds: [...state.expandedNodeIds],
  };
}

export function writeRatingTreeViewState(versionId: string, state: RatingTreeViewState): void {
  stateByVersion.set(versionId, {
    ...state,
    expandedNodeIds: [...state.expandedNodeIds],
  });
}

export function readLastRatingTreeVersionId(): string | null {
  return lastVersionId;
}

export function writeLastRatingTreeVersionId(versionId: string | null): void {
  lastVersionId = versionId;
}

export function clearRatingTreeViewStateForTests(): void {
  stateByVersion.clear();
  lastVersionId = null;
}
