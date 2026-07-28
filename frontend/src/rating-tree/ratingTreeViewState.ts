export interface RatingTreeViewState {
  searchTerm: string;
  expandedNodeIds: string[];
  selectedNodeId: string | null;
}

const stateByVersion = new Map<string, RatingTreeViewState>();

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

export function clearRatingTreeViewStateForTests(): void {
  stateByVersion.clear();
}
