import { describe, expect, it } from "vitest";

import { buildResolutionIndex } from "./resolutionIndex";

// 按来源病害聚合的解析索引。
//
// 这里的关键是区间展开：一条来源病害挂多条活动实例、各绑一件构件。索引对"这条病害
// 绑到哪一件"给出 null（本来就没有单一答案），但**必须**把每条实例的构件都列出来——
// 下游拿它判"绑没绑构件"、拿它建"这件构件适用哪些评定树节点"的表。少了它，展开过的
// 病害会同时被判成"尚未选择实际构件"和"评定树病害不适用于当前实际构件"。

function instance(componentId: string, nodeId: string | null = null) {
  return {
    resolved_defect_instance_id: `inst-${componentId}`,
    bridge_component_id: componentId,
    instance_status: "active",
    overridden_fields: [],
    rating_resolution: {
      present: nodeId !== null,
      status: nodeId ? "matched" : null,
      rating_tree_node_id: nodeId,
      match_method: nodeId ? "source_indicator" : null,
      version: 1,
      content_changed_after_manual_resolution: false,
    },
  };
}

function target(componentId: string, categoryId: string) {
  return { bridge_component_id: componentId, standard_component_category_id: categoryId };
}

describe("buildResolutionIndex", () => {
  it("lists every instance component for a range-expanded defect", () => {
    const index = buildResolutionIndex({
      rating_tree: { version_id: "tree-1" },
      groups: [{
        group_id: "g1",
        status: "bound",
        targets: [
          target("c-1", "h21.component.beam.upper_general"),
          target("c-2", "h21.component.beam.upper_general"),
          target("c-3", "h21.component.beam.upper_general"),
        ],
        members: [{
          source_candidate_id: "d1",
          instances: [instance("c-1", "n1"), instance("c-2", "n1"), instance("c-3", "n1")],
        }],
      }],
    });

    const resolution = index.get("d1")!;
    // 多目标：没有单一答案，这一项按约定留空。
    expect(resolution.bridgeComponentId).toBeNull();
    // 但三件构件都要在，各自带着类别。
    expect(resolution.componentIds).toEqual(["c-1", "c-2", "c-3"]);
    expect(resolution.components.map((c) => c.categoryId))
      .toEqual(Array(3).fill("h21.component.beam.upper_general"));
    expect(resolution.activeInstanceCount).toBe(3);
    // 三条实例匹到同一个节点时，仍给得出这条病害的节点。
    expect(resolution.ratingTreeNodeId).toBe("n1");
  });

  it("keeps the single component answer when there is only one instance", () => {
    const index = buildResolutionIndex({
      rating_tree: { version_id: "tree-1" },
      groups: [{
        group_id: "g1",
        status: "bound",
        targets: [target("c-1", "h21.component.deck.slab")],
        members: [{ source_candidate_id: "d1", instances: [instance("c-1", "n1")] }],
      }],
    });

    const resolution = index.get("d1")!;
    expect(resolution.bridgeComponentId).toBe("c-1");
    expect(resolution.componentIds).toEqual(["c-1"]);
    expect(resolution.standardComponentCategoryId).toBe("h21.component.deck.slab");
  });

  // 被忽略的实例不参与：它不入库，也就不该影响"这条病害绑好了没有"。
  it("ignores instances that are not active", () => {
    const index = buildResolutionIndex({
      rating_tree: { version_id: "tree-1" },
      groups: [{
        group_id: "g1",
        status: "bound",
        targets: [target("c-1", "cat"), target("c-2", "cat")],
        members: [{
          source_candidate_id: "d1",
          instances: [
            instance("c-1", "n1"),
            { ...instance("c-2", "n1"), instance_status: "ignored" },
          ],
        }],
      }],
    });

    const resolution = index.get("d1")!;
    expect(resolution.componentIds).toEqual(["c-1"]);
    expect(resolution.activeInstanceCount).toBe(1);
  });

  // 一条实例都没绑上构件时，才是真的没绑。
  it("reports no components when the group is unresolved", () => {
    const index = buildResolutionIndex({
      rating_tree: { version_id: "tree-1" },
      groups: [{
        group_id: "g1",
        status: "unresolved",
        targets: [],
        members: [{ source_candidate_id: "d1", instances: [] }],
      }],
    });

    const resolution = index.get("d1")!;
    expect(resolution.componentIds).toEqual([]);
    expect(resolution.components).toEqual([]);
    expect(resolution.activeInstanceCount).toBe(0);
  });
});
