import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { applyRatingResolution } from "../../api/resolutionApi";

vi.mock("../../api/resolutionApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../api/resolutionApi")>();
  return { ...actual, applyRatingResolution: vi.fn().mockResolvedValue({}) };
});

import type { RatingTreeNode, RatingTreeNodeSummary } from "../../api/ratingTreeApi";
import { buildDefectPhotoReviewModel } from "../defectPhotoReviewModel";
import { UNRESOLVED, type ResolutionIndex } from "../resolutionIndex";
import { data } from "../testFixtures";
import { DefectDetailEditor } from "./DefectDetailEditor";

// 5.0：构件与评分树结果不在草稿里，由工作区读模型交给 buildDefectPhotoReviewModel。
function resolvedTo(
  candidateId: string,
  nodeId: string,
  categoryId: string,
): ResolutionIndex {
  return new Map([[candidateId, {
    ...UNRESOLVED,
    bridgeComponentId: "component-1",
    standardComponentCategoryId: categoryId,
    ratingTreeVersionId: "tree-version-1",
    ratingTreeNodeId: nodeId,
    ratingMatchMethod: "manual" as const,
    ratingStatus: "matched" as const,
    hasRating: true,
    activeInstanceCount: 1,
  }]]);
}

it("lets a range-split defect without photos be confirmed individually", () => {
  const node: RatingTreeNode = {
    id: "tree-node-water",
    node_key: "org.bridge.defect.water",
    parent_node_id: "tree-group",
    display_number: "5.1.1-13",
    display_name: "水损",
    node_type: "defect",
    sort_order: 13,
    bridge_type_ids: ["bridge-type-1"],
    component_category_ids: ["h21.component.beam"],
    scoring_mode: "inherit_h21",
    h21_indicator_id: "h21.defect.water",
    is_selectable: true,
    is_scoring: true,
    organization_note: "",
    allowed_scales: [1, 2],
    h21_indicator_name: "水损",
    h21_source_table: "表5.1.1",
    scale_descriptions: { "1": "轻微", "2": "明显" },
    deduction_points: { "1": 0, "2": 15 },
    path: [],
    sources: [],
  };
  const draft = data();
  draft.photos = [];
  Object.assign(draft.defects[0], {
    defect_scale: 2,
    photo_references: [],
    warnings: [{
      code: "component_range_split_review_required",
      message: "该病害由构件范围拆分，请人工核对构件、病害和照片关联。",
      severity: "warning",
      target_candidate_id: "defect_0001",
    }],
  });
  const row = buildDefectPhotoReviewModel({
    draft,
    ratingTreeVersionId: "tree-version-1",
    ratingTreeNodes: [node],
    applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([node.id])]]),
    treeRulesReady: true,
    resolution: resolvedTo("defect_0001", node.id, "h21.component.beam"),
    assessmentIssues: [],
  }).rows[0];
  const onConfirm = vi.fn();

  render(
    <DefectDetailEditor
      draft={draft}
      row={row}
      ratingTreeVersionId="tree-version-1"
      applicableNodes={[node]}
      importRecordId="record-1"
      baseUrl="http://backend"
      dispatch={vi.fn()}
      onConfirm={onConfirm}
      onClose={vi.fn()}
    />,
  );

  expect(screen.getByText("该病害由构件范围拆分，请人工核对构件、病害和照片关联。")).toBeInTheDocument();
  expect(screen.getByText("这条病害还没有照片，Word 原文也没有照片编号。")).toBeInTheDocument();
  const confirm = screen.getByRole("button", { name: "确认本组" });
  expect(confirm).toBeEnabled();
  fireEvent.click(confirm);
  expect(onConfirm).toHaveBeenCalledTimes(1);
});

it("uses summary scale rules before the full node detail has loaded", () => {
  const node: RatingTreeNodeSummary = {
    id: "tree-node-drainage",
    node_key: "org.bridge.defect.drainage",
    parent_node_id: "tree-group",
    display_number: "10.5.1-1",
    display_name: "排水不畅",
    node_type: "defect",
    sort_order: 1,
    bridge_type_ids: ["bridge-type-1"],
    component_category_ids: ["h21.component.beam"],
    scoring_mode: "inherit_h21",
    h21_indicator_id: "h21.defect.drainage",
    is_selectable: true,
    is_scoring: true,
    allowed_scales: [1, 2],
    scale_descriptions: { "1": "完好", "2": "排水不畅" },
  };
  const draft = data();
  Object.assign(draft.defects[0], { defect_scale: 1, warnings: [] });
  const row = buildDefectPhotoReviewModel({
    draft,
    ratingTreeVersionId: "tree-version-1",
    ratingTreeNodes: [],
    ratingTreeNodeSummaries: [node],
    applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([node.id])]]),
    treeRulesReady: true,
    resolution: resolvedTo("defect_0001", node.id, "h21.component.beam"),
    assessmentIssues: [],
  }).rows[0];

  render(
    <DefectDetailEditor
      draft={draft}
      row={row}
      ratingTreeVersionId="tree-version-1"
      applicableNodes={[node]}
      importRecordId="record-1"
      baseUrl="http://backend"
      dispatch={vi.fn()}
      onConfirm={vi.fn()}
      onClose={vi.fn()}
    />,
  );

  const scale = screen.getByRole("combobox", { name: "标度" });
  expect(scale).toBeEnabled();
  expect(scale).toHaveValue("1");
  expect(screen.getByRole("option", { name: "1 · 完好" })).toBeInTheDocument();
  expect(screen.getByRole("option", { name: "2 · 排水不畅" })).toBeInTheDocument();
  expect(screen.getByRole("button", { name: "确认本组" })).toBeEnabled();
});

// P1-4 回归：人工选定的评定树节点必须写进评分树解析表。
//
// 此前这里只 dispatch 本地 reducer 改草稿里的病害名称与标度，节点本身没有落库。
// 用户的显式选择于是只活在这一次渲染里：刷新页面或下一次同步，这条病害要么被自动
// 匹配重新盖掉、要么退回未解析——人工判断丢得无声无息。
it("persists a manual node choice to the rating resolution", async () => {
  const node: RatingTreeNodeSummary = {
    id: "tree-node-water",
    node_key: "org.bridge.defect.water",
    parent_node_id: "tree-group",
    display_number: "5.1.1-13",
    display_name: "水损",
    node_type: "defect",
    sort_order: 13,
    bridge_type_ids: ["bridge-type-1"],
    component_category_ids: ["h21.component.beam"],
    scoring_mode: "inherit_h21",
    h21_indicator_id: "h21.defect.water",
    is_selectable: true,
    is_scoring: true,
    allowed_scales: [1, 2],
    scale_descriptions: { "1": "轻微", "2": "明显" },
  };
  const draft = data();
  // 展开成两条活动实例：人选一次，两条都要写，否则只有第一条带着人工判断。
  const resolution = new Map([["defect_0001", {
    ...UNRESOLVED,
    bridgeComponentId: "component-1",
    standardComponentCategoryId: "h21.component.beam",
    ratingTreeVersionId: "tree-version-1",
    activeInstanceCount: 2,
    instances: [
      { instanceId: "instance-1", ratingVersion: 3 },
      { instanceId: "instance-2", ratingVersion: 1 },
    ],
  }]]);
  const row = buildDefectPhotoReviewModel({
    draft,
    ratingTreeVersionId: "tree-version-1",
    ratingTreeNodes: [],
    ratingTreeNodeSummaries: [node],
    applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([node.id])]]),
    treeRulesReady: true,
    resolution,
    assessmentIssues: [],
  }).rows[0];

  const onRatingResolved = vi.fn();
  render(
    <DefectDetailEditor
      draft={draft}
      row={row}
      ratingTreeVersionId="tree-version-1"
      applicableNodes={[node]}
      importRecordId="record-1"
      baseUrl="http://backend"
      dispatch={vi.fn()}
      editLockToken="lock-1"
      onConfirm={vi.fn()}
      onClose={vi.fn()}
      onRatingResolved={onRatingResolved}
    />,
  );

  fireEvent.change(screen.getByRole("combobox", { name: "评定树病害" }), { target: { value: node.id } });

  await waitFor(() => expect(applyRatingResolution).toHaveBeenCalledTimes(2));
  expect(applyRatingResolution).toHaveBeenNthCalledWith(
    1, "http://backend", "record-1", "instance-1",
    {
      expected_version: 3,
      rating_tree_node_id: node.id,
      expected_rating_tree_version_id: "tree-version-1",
    },
    "lock-1");
  // 第二条实例的版本是它自己的，不能套用第一条的。
  expect(applyRatingResolution).toHaveBeenNthCalledWith(
    2, "http://backend", "record-1", "instance-2",
    expect.objectContaining({ expected_version: 1 }), "lock-1");
  await waitFor(() => expect(onRatingResolved).toHaveBeenCalled());
});
