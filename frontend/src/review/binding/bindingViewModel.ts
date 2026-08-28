// 绑定工作区的视图模型。
//
// 解析工作区读模型（§13.1）按「来源构件组」平铺，而绑定界面是按「部件 → 行」两层
// 渲染的。这里只做形状转换，**不重新实现任何判定**：状态、歧义、可否区间展开、允许
// 哪些动作，全部原样取自后端。此前前端自己算过一遍这些，规则一分叉就会出现「界面显示
// 可绑、后端拒绝」，而且只在归一化或类别判定不一致的那几行上出现。
//
// 每行都带上 groupId 与 version：写操作走 `PUT /component-groups/{id}/resolution`，
// 靠这两个值定位与做乐观并发。旧链路用 (部件名, 编号) 定位，那是把展示用的文字当主键。

import type {
  ResolutionWorkspace,
  WorkspaceComponentGroup,
  WorkspaceComponentSummary,
} from "../../api/resolutionApi";

/** 四值状态是界面口径：后端的 unresolved 按是否有多个候选再分成两种。 */
export type ComponentBindingStatus = "bound" | "unmatched" | "ambiguous" | "missing";

export interface BindingComponentSummary {
  bridge_component_id: string;
  component_number: string;
  site_component_type: string;
  site_name: string;
}

export interface SidePairOption {
  label: string;
  bridge_component_ids: string[];
}

export interface BindingRow {
  /** 写操作的定位键与并发版本，来自解析组本身。 */
  group_id: string;
  version: number;
  component_number: string;
  defect_count: number;
  status: ComponentBindingStatus;
  bridge_component_id: string | null;
  bound_component: BindingComponentSummary | null;
  candidate_components: BindingComponentSummary[];
  split_eligible: boolean;
  split_expanded_count: number | null;
  side_pair_option: SidePairOption | null;
  /** 后端给的允许动作与阻断原因，界面据此决定按钮可用性。 */
  allowed_actions: string[];
  blocked_reasons: string[];
}

export interface BindingGroup {
  part_name: string;
  total: number;
  bound: number;
  unmatched: number;
  ambiguous: number;
  missing: number;
  rows: BindingRow[];
}

export interface ComponentBindingOverview {
  inventory_confirmed: boolean;
  /** 契约不变量：非 null 当且仅当 inventory_confirmed 为真。 */
  inventory_revision_id: string | null;
  rating_tree: ResolutionWorkspace["rating_tree"];
  groups: BindingGroup[];
}

function toSummary(item: WorkspaceComponentSummary): BindingComponentSummary {
  return {
    bridge_component_id: item.bridge_component_id,
    component_number: item.component_number,
    site_component_type: item.site_component_type,
    site_name: item.site_name,
  };
}

/**
 * 后端的三值 status 加派生的 ambiguous 标签，摊成界面用的四值。
 *
 * 这不是重新判定：ambiguous 由后端按候选数算好（§4.7），这里只是把它并进状态枚举，
 * 好让界面一个 className 就能上色。
 */
function rowStatus(group: WorkspaceComponentGroup): ComponentBindingStatus {
  if (group.status === "bound") return "bound";
  if (group.status === "missing") return "missing";
  return group.ambiguous ? "ambiguous" : "unmatched";
}

function toRow(group: WorkspaceComponentGroup): BindingRow {
  // 多目标绑定时"绑到哪一件"没有单一答案，bound_component 留空，界面按目标列表展示。
  const single = group.targets.length === 1 ? group.targets[0] : null;
  return {
    group_id: group.group_id,
    version: group.version,
    component_number: group.source_component_number ?? "",
    defect_count: group.members.length,
    status: rowStatus(group),
    bridge_component_id: single?.bridge_component_id ?? null,
    bound_component: single ? toSummary(single) : null,
    candidate_components: group.candidates.map(toSummary),
    split_eligible: group.split_eligible,
    split_expanded_count: group.split_expanded_count,
    side_pair_option: group.side_pair_option,
    allowed_actions: group.allowed_actions,
    blocked_reasons: group.blocked_reasons,
  };
}

/** 把解析工作区读模型摊成绑定界面的「部件 → 行」两层结构。 */
export function toBindingOverview(workspace: ResolutionWorkspace): ComponentBindingOverview {
  const byPart = new Map<string, BindingGroup>();
  // 部件顺序按组在工作区里的出现次序，与后端一致；不在前端另排一遍。
  for (const group of workspace.groups) {
    const partName = group.source_component_name;
    let bucket = byPart.get(partName);
    if (!bucket) {
      bucket = { part_name: partName, total: 0, bound: 0, unmatched: 0, ambiguous: 0, missing: 0, rows: [] };
      byPart.set(partName, bucket);
    }
    const row = toRow(group);
    bucket.rows.push(row);
    bucket.total += 1;
    bucket[row.status] += 1;
  }

  return {
    inventory_confirmed: workspace.inventory_confirmed,
    inventory_revision_id: workspace.inventory_revision_id,
    rating_tree: workspace.rating_tree,
    groups: [...byPart.values()],
  };
}

/**
 * 绑定进度。已标记缺失算「已处理」——台账确无此构件是一个结论，不是待办。
 */
export function bindingProgress(overview: ComponentBindingOverview): {
  total: number;
  settled: number;
  pending: number;
} {
  let total = 0;
  let settled = 0;
  for (const group of overview.groups) {
    total += group.total;
    settled += group.bound + group.missing;
  }
  return { total, settled, pending: total - settled };
}
