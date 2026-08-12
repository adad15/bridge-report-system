import { fireEvent, render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import type { RatingTreeNode, RatingTreeNodeSummary } from "../../api/ratingTreeApi";
import { buildDefectPhotoReviewModel } from "../defectPhotoReviewModel";
import { data } from "../testFixtures";
import { DefectDetailEditor } from "./DefectDetailEditor";

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
    bridge_component_id: "component-1",
    standard_component_category_id: "h21.component.beam",
    rating_tree_version_id: "tree-version-1",
    rating_tree_node_id: node.id,
    rating_tree_match_method: "manual",
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
  Object.assign(draft.defects[0], {
    bridge_component_id: "component-1",
    standard_component_category_id: "h21.component.beam",
    rating_tree_version_id: "tree-version-1",
    rating_tree_node_id: node.id,
    rating_tree_match_method: "manual",
    defect_scale: 1,
    warnings: [],
  });
  const row = buildDefectPhotoReviewModel({
    draft,
    ratingTreeVersionId: "tree-version-1",
    ratingTreeNodes: [],
    ratingTreeNodeSummaries: [node],
    applicableTreeNodeIdsByComponent: new Map([["component-1", new Set([node.id])]]),
    treeRulesReady: true,
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
