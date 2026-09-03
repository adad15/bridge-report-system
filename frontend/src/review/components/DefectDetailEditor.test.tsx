import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { fetchComponentArchive } from "../../api/componentArchiveApi";
import { applySourceRatingResolution } from "../../api/resolutionApi";

vi.mock("../../api/componentArchiveApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../api/componentArchiveApi")>();
  return { ...actual, fetchComponentArchive: vi.fn() };
});

vi.mock("../../api/resolutionApi", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../api/resolutionApi")>();
  return { ...actual, applySourceRatingResolution: vi.fn().mockResolvedValue({}) };
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
  // 标度判定依据已撤下；校对完整度留在判定列，历年病害演变通栏落在字段区与操作栏之间。
  expect(screen.getByLabelText("校对完整度 67%")).toBeInTheDocument();
  expect(screen.getByLabelText("历年病害演变")).toBeInTheDocument();
  expect(screen.queryByText("评定树路径")).not.toBeInTheDocument();
  expect(screen.queryByText("评分规则")).not.toBeInTheDocument();
  const confirm = screen.getByRole("button", { name: "确认" });
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

  const scale = screen.getByRole("combobox", { name: "幅度" });
  expect(scale).toBeEnabled();
  expect(scale).toHaveValue("1");
  expect(screen.getByRole("option", { name: "1 · 完好" })).toBeInTheDocument();
  expect(screen.getByRole("option", { name: "2 · 排水不畅" })).toBeInTheDocument();
  expect(screen.getByRole("button", { name: "确认" })).toBeEnabled();
});

it("shows real prior observations from the bound component archive", async () => {
  const node: RatingTreeNodeSummary = {
    id: "tree-node-crack",
    node_key: "org.bridge.defect.crack",
    parent_node_id: "tree-group",
    display_number: "5.1.1-2",
    display_name: "裂缝",
    node_type: "defect",
    sort_order: 2,
    bridge_type_ids: ["bridge-type-1"],
    component_category_ids: ["h21.component.beam"],
    scoring_mode: "inherit_h21",
    h21_indicator_id: "h21.defect.crack",
    is_selectable: true,
    is_scoring: true,
    allowed_scales: [1, 2],
    scale_descriptions: { "1": "轻微裂缝", "2": "裂缝发展" },
  };
  vi.mocked(fetchComponentArchive).mockResolvedValueOnce({
    component: {
      id: "component-1",
      bridge_id: "bridge-1",
      system_number: "BC-1",
      structure_part: "上部结构",
      component_type: "主梁",
      business_component_code: "2-1#梁",
      current_status: "active",
    },
    ratings: [],
    threads: [{
      id: "thread-1",
      system_number: "DT-1",
      thread_name: "主梁裂缝",
      defect_type: "裂缝",
      defect_location: "第二跨",
      current_status: "active",
      confirmation_status: "confirmed",
      first_seen_year: 2025,
      latest_seen_year: 2025,
      observations: [{
        id: "observation-1",
        system_number: "DO-1",
        inspection_year: 2025,
        defect_thread_id: "thread-1",
        defect_type: "裂缝",
        defect_location: "第二跨",
        scale: "1",
        defect_description: "梁底轻微裂缝",
        review_status: "已确认",
        updated_at: "2025-08-01T00:00:00Z",
        measurements: [],
        photos: [],
      }],
    }],
    unbound_observations: [],
  });
  const draft = data();
  const row = buildDefectPhotoReviewModel({
    draft,
    ratingTreeVersionId: "tree-version-1",
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
      bridgeId="bridge-1"
      dispatch={vi.fn()}
      onConfirm={vi.fn()}
      onClose={vi.fn()}
    />,
  );

  expect(await screen.findByText("标度上升，建议重点关注")).toBeInTheDocument();
  expect(fetchComponentArchive).toHaveBeenCalledWith("http://backend", "component-1");
  expect(screen.getByRole("link", { name: "查看构件完整病害档案 ›" })).toHaveAttribute(
    "href",
    "/bridges/bridge-1/components/component-1",
  );
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

  // 一次请求写完两条实例：逐条发的话后端每次都要取草稿、装评定树、开事务，
  // 区间展开的病害那是 25 遍。命令按**来源病害**编址，不是按实例。
  await waitFor(() => expect(applySourceRatingResolution).toHaveBeenCalledTimes(1));
  expect(applySourceRatingResolution).toHaveBeenCalledWith(
    "http://backend", "record-1", "defect_0001",
    {
      instances: [
        { instance_id: "instance-1", expected_version: 3 },
        // 第二条实例的版本是它自己的，不能套用第一条的——批量写不放宽乐观并发。
        { instance_id: "instance-2", expected_version: 1 },
      ],
      rating_tree_node_id: node.id,
      expected_rating_tree_version_id: "tree-version-1",
    },
    "lock-1");
  await waitFor(() => expect(onRatingResolved).toHaveBeenCalled());
});

it("does not touch the draft when the rating write fails", async () => {
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
  vi.mocked(applySourceRatingResolution).mockRejectedValueOnce(new Error("boom"));
  const draft = data();
  const resolution = new Map([["defect_0001", {
    ...UNRESOLVED,
    bridgeComponentId: "component-1",
    componentIds: ["component-1"],
    components: [{ componentId: "component-1", categoryId: "h21.component.beam" }],
    standardComponentCategoryId: "h21.component.beam",
    ratingTreeVersionId: "tree-version-1",
    activeInstanceCount: 1,
    instances: [{ instanceId: "instance-1", ratingVersion: 0 }],
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
  const dispatch = vi.fn();

  render(
    <DefectDetailEditor
      draft={draft}
      row={row}
      ratingTreeVersionId="tree-version-1"
      applicableNodes={[node]}
      importRecordId="record-1"
      baseUrl="http://backend"
      dispatch={dispatch}
      editLockToken="lock-1"
      onConfirm={vi.fn()}
      onClose={vi.fn()}
    />,
  );

  fireEvent.change(screen.getByRole("combobox", { name: "评定树病害" }), { target: { value: node.id } });

  // 后端拒绝时草稿一个字不能动：错误提示看得见，脏草稿看不见，而用户还能把它保存进去。
  await waitFor(() => expect(applySourceRatingResolution).toHaveBeenCalled());
  expect(dispatch).not.toHaveBeenCalled();
});
