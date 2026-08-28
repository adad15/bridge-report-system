// 按来源病害索引的解析状态。
//
// 5.0 之后"这条病害绑到哪个构件、匹到哪个评分树节点"不在草稿里，而在
// `GET /api/import-records/{id}/resolution-workspace` 的读模型里。校对页的那些纯函数
// 原本直接读 `defect.bridge_component_id`，现在改成从这份索引里查。
//
// 索引按**来源病害**聚合：一条来源病害可能展开成多条实例，校对页关心的是"这一行
// 整体处理完了吗"，所以按全部活动实例取合取——有一条没绑构件，这条病害就还没绑完。

import type {
  RatingResolutionStatus,
  RatingTreeMatchMethod,
} from "../contracts/resolution";

export interface DefectResolution {
  /** 全部活动实例都绑到构件时为该构件 id；多目标或未绑时为 null。 */
  bridgeComponentId: string | null;
  standardComponentCategoryId: string | null;
  /** 全部活动实例都匹到同一个节点时有值；多目标匹到不同节点或未匹时为 null。 */
  ratingTreeNodeId: string | null;
  ratingTreeVersionId: string | null;
  ratingMatchMethod: RatingTreeMatchMethod | null;
  ratingStatus: RatingResolutionStatus | null;
  /** 是否每条活动实例都有评分树解析行。没有解析行是正常状态，不是错误。 */
  hasRating: boolean;
  /** 人工选择后内容变了：保留节点，但提示复核。 */
  contentChangedAfterManualResolution: boolean;
  /** 该来源病害下任一实例脱离来源值的字段并集。 */
  overriddenFields: string[];
  /** 活动实例数。大于一表示这条来源病害展开到了多个构件。 */
  activeInstanceCount: number;
  /** 所属来源构件组，供界面跳转到绑定工作区。 */
  groupId: string | null;
  groupStatus: string | null;
}

export type ResolutionIndex = ReadonlyMap<string, DefectResolution>;

export const EMPTY_RESOLUTION_INDEX: ResolutionIndex = new Map();

/** 没有解析记录时的中性快照，免得每个调用点都写一遍空值判断。 */
export const UNRESOLVED: DefectResolution = {
  bridgeComponentId: null,
  standardComponentCategoryId: null,
  ratingTreeNodeId: null,
  ratingTreeVersionId: null,
  ratingMatchMethod: null,
  ratingStatus: null,
  hasRating: false,
  contentChangedAfterManualResolution: false,
  overriddenFields: [],
  activeInstanceCount: 0,
  groupId: null,
  groupStatus: null,
};

/**
 * 把一次写操作回来的受影响组并进现有索引。
 *
 * 写操作只回受影响对象，直接拿它整份替换会把其余所有组的解析状态一起丢掉，
 * 页面随即显示成"一条都没绑"。
 */
export function mergeResolutionIndex(
  base: ResolutionIndex,
  workspace: ResolutionWorkspaceResponse | null | undefined
): ResolutionIndex {
  const merged = new Map(base);
  for (const [candidateId, resolution] of buildResolutionIndex(workspace)) {
    merged.set(candidateId, resolution);
  }
  return merged;
}

export function resolutionOf(
  index: ResolutionIndex,
  candidateId: string
): DefectResolution {
  return index.get(candidateId) ?? UNRESOLVED;
}

interface WorkspaceInstance {
  instance_status?: string;
  bridge_component_id?: string | null;
  overridden_fields?: string[];
  rating_resolution?: {
    present?: boolean;
    status?: string | null;
    rating_tree_node_id?: string | null;
    match_method?: string | null;
    content_changed_after_manual_resolution?: boolean;
  };
}

interface WorkspaceMember {
  source_candidate_id?: string;
  instances?: WorkspaceInstance[];
}

interface WorkspaceTarget {
  bridge_component_id?: string;
  standard_component_category_id?: string;
}

interface WorkspaceGroup {
  group_id?: string;
  status?: string;
  targets?: WorkspaceTarget[];
  members?: WorkspaceMember[];
}

export interface ResolutionWorkspaceResponse {
  inventory_confirmed?: boolean;
  inventory_revision_id?: string | null;
  rating_tree?: { version_id?: string } | null;
  groups?: WorkspaceGroup[];
}

/** 把工作区读模型压成按来源病害索引的解析快照。 */
export function buildResolutionIndex(
  workspace: ResolutionWorkspaceResponse | null | undefined
): ResolutionIndex {
  const index = new Map<string, DefectResolution>();
  if (!workspace?.groups) return index;
  const treeVersionId = workspace.rating_tree?.version_id ?? null;

  for (const group of workspace.groups) {
    // 规范类别按组所钉的台账版本当场派生（§17.2），随目标一起下发。
    // 校对页靠它算"这件构件适用哪些评定树节点"。
    const categoryByComponent = new Map(
      (group.targets ?? [])
        .filter((target) => target.bridge_component_id)
        .map((target) => [
          target.bridge_component_id as string,
          target.standard_component_category_id || null,
        ])
    );

    for (const member of group.members ?? []) {
      const candidateId = member.source_candidate_id;
      if (!candidateId) continue;
      const active = (member.instances ?? []).filter(
        (instance) => instance.instance_status === "active"
      );

      const componentIds = new Set(
        active.map((instance) => instance.bridge_component_id ?? "")
      );
      const nodeIds = new Set(
        active.map((instance) => instance.rating_resolution?.rating_tree_node_id ?? "")
      );
      const overridden = new Set<string>();
      for (const instance of active) {
        for (const field of instance.overridden_fields ?? []) overridden.add(field);
      }

      // 只有全体一致时才给出单一值：多目标各绑各的构件时，"这条病害绑到哪"本来就
      // 没有单一答案，编一个出来会让界面显示成绑到了其中随便一个。
      const singleComponent =
        componentIds.size === 1 ? [...componentIds][0] || null : null;
      const singleNode = nodeIds.size === 1 ? [...nodeIds][0] || null : null;
      const first = active[0]?.rating_resolution;

      index.set(candidateId, {
        bridgeComponentId: singleComponent,
        standardComponentCategoryId: singleComponent
          ? categoryByComponent.get(singleComponent) ?? null
          : null,
        ratingTreeNodeId: singleNode,
        ratingTreeVersionId: singleNode ? treeVersionId : null,
        ratingMatchMethod: (first?.match_method ?? null) as RatingTreeMatchMethod | null,
        ratingStatus: (first?.status ?? null) as RatingResolutionStatus | null,
        hasRating:
          active.length > 0 &&
          active.every((instance) => instance.rating_resolution?.present === true),
        contentChangedAfterManualResolution: active.some(
          (instance) =>
            instance.rating_resolution?.content_changed_after_manual_resolution === true
        ),
        overriddenFields: [...overridden],
        activeInstanceCount: active.length,
        groupId: group.group_id ?? null,
        groupStatus: group.status ?? null,
      });
    }
  }
  return index;
}
