import { fireEvent, render, screen } from "@testing-library/react";
import { UNRESOLVED } from "../resolutionIndex";
import { expect, it, vi } from "vitest";

import type { RatingTreeNodeSummary } from "../../api/ratingTreeApi";
import type { DefectReviewRow } from "../defectPhotoReviewModel";
import { buildDefectIssueGroups } from "../defectIssueGroups";
import { data } from "../testFixtures";
import { DefectIssueGroupList } from "./DefectIssueGroupList";

function row(candidateId: string, componentId: string): DefectReviewRow {
  return {
    candidateId,
    // 5.0：解析状态挂行上。
    resolution: {
      ...UNRESOLVED,
      bridgeComponentId: componentId,
      standardComponentCategoryId: "h21.component.deck.slab",
      activeInstanceCount: 1,
    },
    defect: {
      ...data().defects[0],
      candidate_id: candidateId,
      component_number: candidateId,
      source_defect_group_id: "group-a",
      source_defect_group_number: "5.1.1",
      source_defect_indicator_id: "indicator-a",
      source_defect_indicator_number: "5.1.1-8",
      defect_type: "",
      defect_description: "存在黑点痕迹",
      photo_references: [],
    },
    photos: [],
    problems: [{ code: "rating_tree_node_required", category: "defect_type", message: "尚未选择评定树病害。" }],
    confirmEligible: false,
    batchEligible: false,
    status: "needs_attention",
    matchState: "unmatched",
    matchLabel: "未找到匹配",
    matchResult: null,
    matchCandidates: [],
    photoCards: [],
    ratingTreeNode: null,
  };
}

const node: RatingTreeNodeSummary = {
  id: "tree-node-other",
  node_key: "org.bridge.defect.other",
  parent_node_id: "tree-group",
  display_number: "5.1.1-9",
  display_name: "其他病害",
  node_type: "defect",
  sort_order: 90,
  bridge_type_ids: ["bridge-type-1"],
  component_category_ids: ["h21.component.deck.slab"],
  scoring_mode: "non_scoring",
  h21_indicator_id: null,
  is_selectable: true,
  is_scoring: false,
};

it("assigns a common applicable node to every defect in an exact source group", () => {
  const groups = buildDefectIssueGroups([
    row("1-1#板", "component-1"),
    row("1-2#板", "component-2"),
  ]);
  const onApplyNode = vi.fn();
  render(
    <DefectIssueGroupList
      groups={groups}
      nodesByComponent={new Map([
        ["component-1", [node]],
        ["component-2", [node]],
      ])}
      onApplyNode={onApplyNode}
      onConfirmGroup={vi.fn()}
      onOpenDefect={vi.fn()}
    />,
  );

  expect(screen.getByText("1 个问题组")).toBeInTheDocument();
  fireEvent.change(screen.getByRole("combobox", { name: "为 存在黑点痕迹 选择评定树病害" }), {
    target: { value: node.id },
  });
  fireEvent.click(screen.getByRole("button", { name: "应用到本组 2 条" }));

  expect(onApplyNode).toHaveBeenCalledWith(groups[0], node);
});

it("offers one group confirmation for range-split defects without requiring photos", () => {
  const rows = [
    row("1-1#板", "component-1"),
    row("1-2#板", "component-2"),
  ];
  for (const item of rows) {
    item.resolution = {
      ...item.resolution,
      ratingTreeNodeId: node.id,
      ratingMatchMethod: "source_indicator",
    };
    item.defect.defect_type = "渗水泛碱";
    item.problems = [{
      code: "component_range_split_review_required",
      category: "other",
      message: "该病害由构件范围拆分，请人工核对构件、病害和照片关联。",
    }];
    item.confirmEligible = true;
    item.matchState = "auto_bound";
    item.matchLabel = "来源软件标注";
  }
  const groups = buildDefectIssueGroups(rows);
  const onConfirmGroup = vi.fn();

  render(
    <DefectIssueGroupList
      groups={groups}
      nodesByComponent={new Map()}
      onApplyNode={vi.fn()}
      onConfirmGroup={onConfirmGroup}
      onOpenDefect={vi.fn()}
    />,
  );

  fireEvent.click(screen.getByRole("button", { name: "确认本组可确认项（2）" }));

  expect(onConfirmGroup).toHaveBeenCalledWith(groups[0]);
});
