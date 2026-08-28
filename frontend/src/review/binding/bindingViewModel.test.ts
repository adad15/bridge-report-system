import { describe, expect, it } from "vitest";

import type {
  ResolutionWorkspace,
  WorkspaceComponentGroup,
} from "../../api/resolutionApi";
import { bindingProgress, toBindingOverview } from "./bindingViewModel";

function group(overrides: Partial<WorkspaceComponentGroup> = {}): WorkspaceComponentGroup {
  return {
    group_id: "group-1",
    source_component_name: "栏杆、护栏",
    source_component_number: "两侧栏杆",
    normalized_component_number: "两侧栏杆",
    resolution_mode: "single",
    status: "unresolved",
    match_method: null,
    inventory_revision_id: "revision-1",
    version: 3,
    ambiguous: false,
    split_eligible: false,
    split_expanded_count: null,
    side_pair_option: null,
    targets: [],
    candidates: [],
    members: [],
    allowed_actions: ["bind", "mark_missing"],
    blocked_reasons: [],
    ...overrides,
  };
}

function workspace(groups: WorkspaceComponentGroup[]): ResolutionWorkspace {
  return {
    import_record_id: "record-1",
    bridge_id: "bridge-1",
    draft_version: 1,
    inventory_confirmed: true,
    inventory_revision_id: "revision-1",
    rating_tree: {
      version_id: "tree-1", tree_name: "单位桥梁评定树", package_version: "1.0.3",
      h21_package_version: "2011", maintenance_package_version: "2021",
    },
    groups,
    parts: [],
    progress: {
      group_count: groups.length, bound_count: 0, unresolved_count: 0, ambiguous_count: 0,
      missing_count: 0, instance_count: 0, active_instance_count: 0,
      rating_matched_count: 0, rating_unresolved_count: 0, rating_missing_count: 0,
    },
  };
}

const component = (id: string, number: string) => ({
  bridge_component_id: id,
  component_number: number,
  site_component_type: "栏杆",
  site_name: "栏杆",
  standard_component_category_id: "h21.component.deck.railing",
  standard_bridge_type_id: "h21.bridge_type.beam",
});

describe("toBindingOverview", () => {
  // 界面按「部件 → 行」两层渲染，读模型是平铺的。归拢时部件顺序跟着组的出现次序，
  // 不在前端另排一遍——排序口径由后端一处说了算。
  it("groups rows under their part in workspace order", () => {
    const overview = toBindingOverview(workspace([
      group({ group_id: "g1", source_component_name: "栏杆、护栏", source_component_number: "两侧栏杆" }),
      group({ group_id: "g2", source_component_name: "上部承重构件", source_component_number: "1-1#梁" }),
      group({ group_id: "g3", source_component_name: "栏杆、护栏", source_component_number: "左侧栏杆" }),
    ]));

    expect(overview.groups.map((g) => g.part_name)).toEqual(["栏杆、护栏", "上部承重构件"]);
    expect(overview.groups[0].rows.map((r) => r.component_number))
      .toEqual(["两侧栏杆", "左侧栏杆"]);
  });

  // 后端的三值 status 加派生的 ambiguous 摊成界面的四值。ambiguous 是后端按候选数
  // 算好的派生标签，这里只是并进枚举，不自己数候选。
  it("splits unresolved into unmatched and ambiguous by the backend flag", () => {
    const overview = toBindingOverview(workspace([
      group({ group_id: "g1", status: "unresolved", ambiguous: false }),
      group({ group_id: "g2", status: "unresolved", ambiguous: true }),
      group({ group_id: "g3", status: "bound", targets: [component("c1", "1-1#梁")] }),
      group({ group_id: "g4", status: "missing" }),
    ]));

    expect(overview.groups[0].rows.map((r) => r.status))
      .toEqual(["unmatched", "ambiguous", "bound", "missing"]);
    expect(overview.groups[0]).toMatchObject({
      total: 4, bound: 1, unmatched: 1, ambiguous: 1, missing: 1,
    });
  });

  // 写操作走 group_id + version 做乐观并发；旧链路拿 (部件名, 编号) 当主键，
  // 那是把展示用的文字当身份。
  it("carries the group id and version onto every row", () => {
    const overview = toBindingOverview(workspace([group({ group_id: "g7", version: 9 })]));
    expect(overview.groups[0].rows[0]).toMatchObject({ group_id: "g7", version: 9 });
  });

  // 多目标绑定时"绑到哪一件"没有单一答案，编出一个来会让界面显示成绑到了其中随便一个。
  it("leaves bound_component empty when a group has several targets", () => {
    const overview = toBindingOverview(workspace([
      group({
        group_id: "g1",
        status: "bound",
        targets: [component("c-left", "左侧栏杆"), component("c-right", "右侧栏杆")],
      }),
    ]));

    const row = overview.groups[0].rows[0];
    expect(row.status).toBe("bound");
    expect(row.bound_component).toBeNull();
    expect(row.bridge_component_id).toBeNull();
  });

  it("passes the side pair option through untouched", () => {
    const option = { label: "整体绑定到 左侧栏杆 与 右侧栏杆", bridge_component_ids: ["c-left", "c-right"] };
    const overview = toBindingOverview(workspace([group({ side_pair_option: option })]));
    expect(overview.groups[0].rows[0].side_pair_option).toEqual(option);
  });

  it("keeps defect count from the group members", () => {
    const overview = toBindingOverview(workspace([
      group({ members: [
        { member_id: "m1", source_candidate_id: "d1", source_order: 0, instances: [] },
        { member_id: "m2", source_candidate_id: "d2", source_order: 1, instances: [] },
      ] }),
    ]));
    expect(overview.groups[0].rows[0].defect_count).toBe(2);
  });
});

describe("bindingProgress", () => {
  // 已标记缺失算已处理：台账确无此构件是一个结论，不是待办。
  it("counts marked-missing as settled", () => {
    const overview = toBindingOverview(workspace([
      group({ group_id: "g1", status: "bound", targets: [component("c1", "1-1#梁")] }),
      group({ group_id: "g2", status: "missing" }),
      group({ group_id: "g3", status: "unresolved" }),
    ]));

    expect(bindingProgress(overview)).toEqual({ total: 3, settled: 2, pending: 1 });
  });
});
